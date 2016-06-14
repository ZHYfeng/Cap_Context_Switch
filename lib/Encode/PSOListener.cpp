/*
 * PSOListener.cpp
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#include "PSOListener.h"
#include "klee/Expr.h"
#include "Trace.h"
#include "Transfer.h"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <malloc.h>
#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/DebugInfo.h"


using namespace std;
using namespace llvm;

#define EVENTS_DEBUG 0

#define PTR 0
#define DEBUGSTRCPY 0
#define COND_DEBUG 0

namespace klee {

Event* lastEvent;

PSOListener::PSOListener(Executor* executor, RuntimeDataManager* rdManager) :
		BitcodeListener(), executor(executor), rdManager(rdManager) {
	// TODO Auto-generated constructor stub
	Kind = PSOListenerKind;
}

PSOListener::~PSOListener() {
	// TODO Auto-generated destructor stub
	for (map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.begin(),
			bre = barrierRecord.end(); bri != bre; bri++) {
		if (bri->second) {
			delete bri->second;
		}
	}
}

//消息响应函数，在被测程序解释执行之前调用
void PSOListener::beforeRunMethodAsMain(ExecutionState &initialState) {

	//收集全局变量初始化值
	Module* m = executor->kmodule->module;
	rdManager->createNewTrace(executor->executionNum);
	for (Module::global_iterator i = m->global_begin(), e = m->global_end();
			i != e; ++i) {
		if (i->hasInitializer() && i->getName().str().at(0) != '.') {
			MemoryObject *mo = executor->globalObjects.find(i)->second;
			Constant* initializer = i->getInitializer();
			uint64_t address = mo->address;
			handleInitializer(initializer, mo, address);
		}
	}


	unsigned traceNum = executor->executionNum;
	cerr << endl;
	cerr << "************************************************************************\n";
	cerr << "第" << traceNum << "次执行,路径文件为trace" << traceNum << ".txt";
	if (traceNum == 1) {
		cerr << " 初始执行" << endl;
	} else {
		cerr << " 前缀执行,前缀文件为prefix" << executor->prefix->getName() << ".txt" << endl;
	}
	cerr << "************************************************************************\n";
	cerr << endl;
}

//指令调用消息响应函数，在指令解释执行前被调用
void PSOListener::executeInstruction(ExecutionState &state, KInstruction *ki) {
	Trace* trace = rdManager->getCurrentTrace();
	Instruction* inst = ki->inst;
	Thread* thread = state.currentThread;
	Event* item = NULL;
	KModule* kmodule = executor->kmodule;
	lastEvent = NULL;

//	cerr << "thread id : " << thread->threadId << " line : " << ki->info->line;
//	inst->dump();

	// filter the instruction linked by klee force-import such as klee_div_zero_check
	if (kmodule->kleeFunctions.find(inst->getParent()->getParent())
			== kmodule->kleeFunctions.end()
			&& kmodule->intrinsicFunctions.find(inst->getParent()->getParent())
					== kmodule->intrinsicFunctions.end()) {
		//cerr << " dir: " << dir << " file: " << file << " line: " << line << endl;
		item = trace->createEvent(thread->threadId, ki, Event::NORMAL);
	} else {
		item = trace->createEvent(thread->threadId, ki, Event::IGNORE);
	}

	vector<Event*> frontVirtualEvents, backVirtualEvents; // the virtual event which should be inserted before/behind item
	frontVirtualEvents.reserve(10);
	backVirtualEvents.reserve(10);
	switch (inst->getOpcode()) {
	case Instruction::Call: {
//		inst->dump();
		CallSite cs(inst);
		Value *fp = cs.getCalledValue();
		Function *f = executor->getTargetFunction(fp, state);
		if (!f) {
			ref<Expr> expr = executor->eval(ki, 0, stack[thread->threadId]).value;
			ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr.get());
			uint64_t functionPtr = constExpr->getZExtValue();
			f = (Function*) functionPtr;
		}
//		if (executor->kmodule->functionMap.find(f) == executor->kmodule->functionMap.end()) {
//			assert(0 && "function not exist");
//		}
		item->calledFunction = f;
		if (f && f->isDeclaration()
				&& f->getIntrinsicID() == Intrinsic::not_intrinsic
//				&& !f->isDeclaration()
				&& kmodule->kleeFunctions.find(f)
						== kmodule->kleeFunctions.end()
				&& kmodule->intrinsicFunctions.find(f)
						== kmodule->intrinsicFunctions.end()
						) {

			item->isFunctionWithSourceCode = false;
		}
//		std::cerr<<"isFunctionWithSourceCode : "<<item->isFunctionWithSourceCode<<"\n";

		//by hy
		//store all call arg
		if (!item->isFunctionWithSourceCode) {
			unsigned numArgs = cs.arg_size();
			item->instParameter.reserve(numArgs);
			for (unsigned j = 0; j < numArgs; ++j) {
				item->instParameter.push_back(executor->eval(ki, j + 1, stack[thread->threadId]).value);
			}
		}
//		std::cerr<<"call name : "<< f->getName().str().c_str() <<"\n";
//		if(f->getName().str() == "open"){
//			cerr<<f->getName().str()<<"\n";
//			ref<Expr> Address = executor->eval(ki, 1, stack[thread->threadId]).value;
//			ObjectPair op;
//			executor->getMemoryObject(op, state, Address);
//			const ObjectState* destos = op.second;
//			const MemoryObject* mo = op.first;
//			Expr::Width size = 8;
//			int i = 0;
//			for ( ; i < mo->size; i++) {
//				ref<Expr> value = destos->read(i, size);
//				if(value->isZero()){
//					break;
//				}
//						ConstantExpr *value1 = cast<ConstantExpr>(value);
//						uint64_t scraddress = value1->getZExtValue();
//						char valuec = scraddress;
//					std::cerr<<"value : "<<valuec<<std::endl;
//			}
//		}
		if (f->getName().str() == "pthread_create") {
//			CallInst* calli = dyn_cast<CallInst>(inst);
//			assert(
//					calli->getNumArgOperands() == 4
//							&& "pthread_create has 4 params");
//			Value* threadEntranceFP = calli->getArgOperand(2);
//			Function *threadEntrance = executor->getTargetFunction(
//					threadEntranceFP, state);
//			if (!threadEntrance) {
//				ref<Expr> param = executor->eval(ki, 3, stack[thread->threadId]).value;
//				ConstantExpr* functionPtr = dyn_cast<ConstantExpr>(param);
//				threadEntrance = (Function*)(functionPtr->getZExtValue());
//				//assert(0 && "thread entrance not exist");
//				//						KFunction *kf =
//				//								executor->kmodule->functionMap[threadEntrance];
//				//runtime.pushStackFrame(kf->function, kf->numRegisters, executor->nextThreadId);
//			}
			ref<Expr> pthreadAddress = executor->eval(ki, 1, stack[thread->threadId]).value;
			ObjectPair pthreadop;
			bool success = executor->getMemoryObject(pthreadop, state, pthreadAddress);
			if (success) {
//				const ObjectState* pthreados = pthreadop.second;
				const MemoryObject* pthreadmo = pthreadop.first;
				ConstantExpr* realAddress = dyn_cast<ConstantExpr>(pthreadAddress.get());
				uint64_t key = realAddress->getZExtValue();
				if (executor->isGlobalMO(pthreadmo)) {
					item->isGlobal = true;
				}
				string varName = createVarName(pthreadmo->id, pthreadAddress, item->isGlobal);
				string varFullName;
				if (item->isGlobal) {
					unsigned loadTime = getLoadTime(key);
					varFullName = createGlobalVarFullName(varName, loadTime,
							false);
				}
				item->globalName = varFullName;
				item->name = varName;
			}
		} else if (f->getName().str() == "pthread_join") {
			CallInst* calli = dyn_cast<CallInst>(inst);
			IntegerType* paramType =
					(IntegerType*) (calli->getArgOperand(0)->getType());
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ConstantExpr* joinedThreadIdExpr = dyn_cast<ConstantExpr>(param);
			uint64_t joinedThreadId = joinedThreadIdExpr->getZExtValue(
					paramType->getBitWidth());
			trace->insertThreadCreateOrJoin(make_pair(item, joinedThreadId),
					false);
		} else if (f->getName().str() == "pthread_cond_wait") {
			ref<Expr> param;
			ObjectPair op;
			Event *lock;
//			Event *unlock;
			bool success;
			//get lock
			param = executor->eval(ki, 2, stack[thread->threadId]).value;
			success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
//				unlock = trace->createEvent(thread->threadId, ki,
//						Event::VIRTUAL);
//				unlock->calledFunction = f;
//				string temp = item->eventName;
//				item->eventName = unlock->eventName;
//				unlock->eventName = temp;
//				unlock->eventName = item->eventName;
//				frontVirtualEvents.push_back(unlock);
				lock = trace->createEvent(thread->threadId, ki, Event::VIRTUAL);
				lock->calledFunction = f;
				backVirtualEvents.push_back(lock);
				trace->insertLockOrUnlock(thread->threadId, mutexName, item,
						false);
				trace->insertLockOrUnlock(thread->threadId, mutexName, lock,
						true);
			} else {
				assert(0 && "mutex not exist");
			}
			//get cond
			param = executor->eval(ki, 1, stack[thread->threadId]).value;
			success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
				trace->insertWait(condName, item, lock);
			} else {
				assert(0 && "cond not exist");
			}
#if COND_DEBUG
			ki->inst->dump();
			cerr << "event name : " << item->eventName << "\n";
			cerr << "wait : " << item->condName << "\n";
#endif
		} else if (f->getName().str() == "pthread_cond_signal") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ObjectPair op;
			bool success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
				trace->insertSignal(condName, item);
			} else {
				assert(0 && "cond not exist");
			}
#if COND_DEBUG
			ki->inst->dump();
			cerr << "event name : " << item->eventName << "\n";
			cerr << "signal  : " << item->condName << "\n";
#endif
		} else if (f->getName().str() == "pthread_cond_broadcast") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ObjectPair op;
			bool success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
				trace->insertSignal(condName, item);
			} else {
				assert(0 && "cond not exist");
			}
#if COND_DEBUG
			ki->inst->dump();
			cerr << "event name : " << item->eventName << "\n";
			cerr << "broadcast cond  : " << item->condName << "\n";
#endif
		} else if (f->getName().str() == "pthread_mutex_lock") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ObjectPair op;
			bool success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
				trace->insertLockOrUnlock(thread->threadId, mutexName, item,
						true);
			} else {
				assert(0 && "mutex not exist");
			}
		} else if (f->getName().str() == "pthread_mutex_unlock") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ObjectPair op;
			bool success = executor->getMemoryObject(op, state, param);
			if (success) {
				const MemoryObject* mo = op.first;
				string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
				trace->insertLockOrUnlock(thread->threadId, mutexName, item,
						false);
			} else {
				assert(0 && "mutex not exist");
			}
		} else if (f->getName().str() == "pthread_barrier_wait") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ConstantExpr* barrierAddressExpr = dyn_cast<ConstantExpr>(param);
			uint64_t barrierAddress = barrierAddressExpr->getZExtValue();
			map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.find(
					barrierAddress);
			BarrierInfo* barrierInfo = NULL;
			if (bri == barrierRecord.end()) {
				barrierInfo = new BarrierInfo();
				barrierRecord.insert(make_pair(barrierAddress, barrierInfo));
			} else {
				barrierInfo = bri->second;
			}
			string barrierName = createBarrierName(barrierAddress,
					barrierInfo->releasedCount);
			trace->insertBarrierOperation(barrierName, item);
			//					cerr << "insert " << barrierName << " " << item->eventName << endl;
			bool isReleased = barrierInfo->addWaitItem();
			if (isReleased) {
				barrierInfo->addReleaseItem();
			}
		} else if (f->getName().str() == "pthread_barrier_init") {
			ref<Expr> param = executor->eval(ki, 1, stack[thread->threadId]).value;
			ConstantExpr* barrierAddressExpr = dyn_cast<ConstantExpr>(param);
			uint64_t barrierAddress = barrierAddressExpr->getZExtValue();
			map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.find(
					barrierAddress);
			BarrierInfo* barrierInfo = NULL;
			if (bri == barrierRecord.end()) {
				barrierInfo = new BarrierInfo();
				barrierRecord.insert(make_pair(barrierAddress, barrierInfo));
			} else {
				barrierInfo = bri->second;
			}

			param = executor->eval(ki, 3, stack[thread->threadId]).value;
			ConstantExpr* countExpr = dyn_cast<ConstantExpr>(param);
			barrierInfo->count = countExpr->getZExtValue();
			//				} else if (f->getName().str() == "printf") {
			//					//find variable used in printf and insert its value into rdManaget
			//					// have serious bug! need to repair!
			//					//ylc
			//					CallInst* calli = dyn_cast<CallInst>(inst);
			//					unsigned argNum = calli->getNumArgOperands();
			//					//printf is external, having no element in stack, so the top of stack is the caller
			//					KFunction* kf = state.stack.back().kf;
			//					//skip the first param : format
			//					for (unsigned i = 1; i < argNum; i++) {
			//						ref<Expr> param = executor->eval(ki, i + 1, state).value;
			//						Type* paramType = calli->getArgOperand(i)->getType();
			//						Constant* constant = expr2Constant(param.get(),
			//								paramType);
			//						int index = ki->operands[i + 1]
			//								- kf->function->arg_size();
			//						KInstruction* paramLoad = kf->instructions[index];
			//						string name;
			//						if (paramLoad->inst->getOpcode() == Instruction::Load) {
			//							ref<Expr> address = executor->eval(paramLoad, 0,
			//									state).value;
			//							ObjectPair op;
			//							bool success = executor->getMemoryObject(op, state, address);
			//							if (success) {
			//								const MemoryObject *mo = op.first;
			//								name = createVarName(mo->id, address,
			//										executor->isGlobalMO(mo));
			//							} else {
			//								assert(0 && "resolve param address failed");
			//							}
			//						} else {
			//							name = createTemporalName();
			//						}
			//						item->outputParams.push_back(constant);
			//						rdManager.insertPrintfParam(name, constant);
			//					}
		} else if (f->getName() == "make_taint") {
			ref<Expr> address = executor->eval(ki, 1, stack[thread->threadId]).value;
			ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address.get());
			if (realAddress) {
				uint64_t key = realAddress->getZExtValue();
				ObjectPair op;
				bool success = executor->getMemoryObject(op, state, address);
				if (success) {
					const MemoryObject *mo = op.first;
					if (executor->isGlobalMO(mo)) {
						item->isGlobal = true;
					}
					//					if (mo->isGlobal) {
					//						insertGlobalVariable(address, inst->getOperand(1)->getType()->getPointerElementType());
					//					}
					string varName = createVarName(mo->id, address, item->isGlobal);
					string varFullName;
					if (item->isGlobal) {
						unsigned storeTime = getStoreTimeForTaint(key);
						if (storeTime == 0) {
							varFullName = varName + "_Init_tag";
						} else {
							varFullName = createGlobalVarFullName(varName, storeTime,
									true);
						}

					}
					item->globalName = varFullName;
					item->name = varName;
				}
			}
		} else if (kmodule->kleeFunctions.find(f)
				!= kmodule->kleeFunctions.end()) {
			item->eventType = Event::IGNORE;
		}
		//cerr << item->calledFunction->getName().str() << " " << item->isUserDefinedFunction << endl;
		break;
	}

	case Instruction::Ret: {
		//runtime.popStackFrame(state.threadId);
		break;
	}

	case Instruction::Add: {
		break;
	}

	case Instruction::Sub: {
		break;
	}

	case Instruction::Mul: {
		break;
	}

	case Instruction::GetElementPtr: {
		//对于对数组解引用的GetElementPtr，获取其所有元素的地址，存放在Event中
		//目前只处理一维数组,多维数组及指针数组赞不考虑，该处理函数存在问题
		//ylc
		GetElementPtrInst* gp = dyn_cast<GetElementPtrInst>(inst);
		KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
		for (std::vector<std::pair<unsigned, uint64_t> >::iterator it =
				kgepi->indices.begin(), ie = kgepi->indices.end(); it != ie;
				++it) {
			ref<Expr> index = executor->eval(ki, it->first, stack[thread->threadId]).value;
//			std::cerr << "kgepi->index : " << index << std::endl;
			item->instParameter.push_back(index);
		}
		if (kgepi->offset) {
			item->instParameter.push_back(ConstantExpr::create(kgepi->offset, 64));
		}
		item->isConditionInst = true;
		item->brCondition = true;
		Type* pointedTy =
				gp->getPointerOperand()->getType()->getPointerElementType();
		switch (pointedTy->getTypeID()) {
		case Type::ArrayTyID: {
			//只处理索引值是变量的getElementPtr属性，因为索引值是常量时其访问的元素不会随线程交织变化
			if (inst->getOperand(2)->getValueID() != Value::ConstantIntVal) {
//				uint64_t num = pointedTy->getArrayNumElements();
//				uint64_t elementBitWidth =
//						executor->kmodule->targetData->getTypeSizeInBits(
//								pointedTy->getArrayElementType());
//				Expr* expr = executor->eval(ki, 0, stack[thread->threadId]).value.get();
//				ConstantExpr* cexpr = dyn_cast<ConstantExpr>(expr);
//				uint64_t base = cexpr->getZExtValue();
//				expr = executor->eval(ki, 2, stack[thread->threadId]).value.get();
//				cexpr = dyn_cast<ConstantExpr>(expr);
//				uint64_t selectedIndex = cexpr->getZExtValue();
			}
			break;
		}

		case Type::HalfTyID:
		case Type::FloatTyID:
		case Type::DoubleTyID:
		case Type::X86_FP80TyID:
		case Type::FP128TyID:
		case Type::PPC_FP128TyID:
		case Type::IntegerTyID: {
			if (inst->getOperand(1)->getValueID() != Value::ConstantIntVal) {
				Type* pointedTy =
						inst->getOperand(0)->getType()->getPointerElementType();
				uint64_t elementBitWidth =
						executor->kmodule->targetData->getTypeSizeInBits(
								pointedTy);
				Expr* expr = executor->eval(ki, 0, stack[thread->threadId]).value.get();
				ConstantExpr* cexpr = dyn_cast<ConstantExpr>(expr);
				uint64_t base = cexpr->getZExtValue();
				expr = executor->eval(ki, 1, stack[thread->threadId]).value.get();
				cexpr = dyn_cast<ConstantExpr>(expr);
				uint64_t index = cexpr->getZExtValue();
				uint64_t startAddress = base + index * elementBitWidth / 8;
				ObjectPair op;
//				cerr << "base = " << base << " index = " << index << " startAddress = " << startAddress << " pointerWidth = " << Context::get().getPointerWidth() << endl;
				bool success = executor->getMemoryObject(op, state,
						ConstantExpr::create(startAddress,
								Context::get().getPointerWidth()));
				if (success) {
					const MemoryObject* mo = op.first;
					uint64_t elementNum = mo->size / (elementBitWidth / 8);
					if (elementNum > 1) {
//						uint64_t selectedIndex = (startAddress - mo->address)
//								/ (elementBitWidth / 8);
					}
				} else {
					inst->dump();
					cerr << "access address: " << startAddress
							<< "has not been allocated" << endl;
					//assert(0 && "the address has not been allocated");
				}
			}
			break;
		}

		default: {
			//cerr << "unhandled type " << pointedTy->getTypeID() << endl;
		}

		}
		break;
	}

	case Instruction::ICmp: {
		break;
	}

	case Instruction::BitCast: {
		break;
	}

	case Instruction::PHI: {
		break;
	}

	case Instruction::Br: {
		BranchInst *bi = dyn_cast<BranchInst>(inst);
		if (!bi->isUnconditional()) {
			item->isConditionInst = true;
			ref<Expr> param = executor->eval(ki, 0, stack[thread->threadId]).value;
			ConstantExpr* condition = dyn_cast<ConstantExpr>(param);
			if (condition->isTrue()) {
				item->brCondition = true;
			} else {
				item->brCondition = false;
			}
		}
		break;
	}

	case Instruction::Load: {
		LoadInst* li = dyn_cast<LoadInst>(ki->inst);
		if (li->getPointerOperand()->getName().equals("stdout")
				|| li->getPointerOperand()->getName().equals("stderr")) {
			item->eventType = Event::IGNORE;
		} else {
			ref<Expr> address = executor->eval(ki, 0, stack[thread->threadId]).value;
			ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address.get());
			if (realAddress) {
//				Expr::Width size = executor->getWidthForLLVMType(
//						ki->inst->getType());
//				ref<Expr> value = readExpr(state, address, size);
//				cerr << "load value : " << value << "\n";
				uint64_t key = realAddress->getZExtValue();
				ObjectPair op;
				bool success = executor->getMemoryObject(op, state, address);
				if (success) {
					const MemoryObject *mo = op.first;
					if (executor->isGlobalMO(mo)) {
						item->isGlobal = true;
//						if(mo->isArg){
//							item->isArg = 1;
//						}
					}
					//					if (mo->isGlobal) {
					//						insertGlobalVariable(address, inst->getOperand(0)->getType()->getPointerElementType());
					//					}
					string varName = createVarName(mo->id, address,
							item->isGlobal);
					string varFullName;
					if (item->isGlobal) {
						unsigned loadTime = getLoadTime(key);
						varFullName = createGlobalVarFullName(varName, loadTime,
								false);
					}
					item->globalName = varFullName;
					item->name = varName;

#if PTR
					if (item->isGlobal) {
#else
					if (!inst->getType()->isPointerTy() && item->isGlobal) {
#endif
						trace->insertReadSet(varName, item);
					}
					if (inst->getOperand(0)->getValueID()
							== Value::InstructionVal
									+ Instruction::GetElementPtr) {

					}
					//					if (item->vectorInfo) {
					//						vector<uint64_t> v = item->vectorInfo->getAllPossibleAddress();
					//						for(vector<uint64_t>::iterator vi = v.begin(), ve = v.end(); vi != ve; vi++) {
					//							cerr << *vi << " ";
					//						}
					//						cerr << endl;
					//					}
					//					cerr << "address = " << realAddress->getZExtValue() << endl;
				} else {
					cerr << "Load address = " << realAddress->getZExtValue()
							<< endl;
					assert(0 && "load resolve unsuccess");
				}
			} else {
				assert(0 && " address is not const");
			}
		}
		break;
	}

	case Instruction::Store: {
		ref<Expr> value = executor->eval(ki, 0, stack[thread->threadId]).value;
		item->instParameter.push_back(value);
		ref<Expr> address = executor->eval(ki, 1, stack[thread->threadId]).value;
		ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address.get());
		if (realAddress) {
			uint64_t key = realAddress->getZExtValue();
			ObjectPair op;
			bool success = executor->getMemoryObject(op, state, address);
			if (success) {
				const MemoryObject *mo = op.first;
				if (executor->isGlobalMO(mo)) {
					item->isGlobal = true;
				}
				//					if (mo->isGlobal) {
				//						insertGlobalVariable(address, inst->getOperand(1)->getType()->getPointerElementType());
				//					}
				string varName = createVarName(mo->id, address, item->isGlobal);
				string varFullName;
				if (item->isGlobal) {
					unsigned storeTime = getStoreTime(key);
					varFullName = createGlobalVarFullName(varName, storeTime,
							true);
				}
				item->globalName = varFullName;
				item->name = varName;
#if PTR
				if (item->isGlobal) {
#else
				if (!inst->getOperand(0)->getType()->isPointerTy() && item->isGlobal) {
#endif
					trace->insertWriteSet(varName, item);
				}
				//					if (item->vectorInfo) {
				//						vector<uint64_t> v = item->vectorInfo->getAllPossibleAddress();
				//						for(vector<uint64_t>::iterator vi = v.begin(), ve = v.end(); vi != ve; vi++) {
				//							cerr << *vi << " ";
				//						}
				//						cerr << endl;
				//					}
				//					cerr << "address = " << realAddress->getZExtValue() << endl;
			} else {
				cerr << "Store address = " << realAddress->getZExtValue()
						<< endl;
				assert(0 && "store resolve unsuccess");
			}
		} else {
			assert(0 && " address is not const");
		}
		break;
	}
	case Instruction::Switch: {
//		SwitchInst *si = cast<SwitchInst>(inst);
		ref<Expr> cond = executor->eval(ki, 0, stack[thread->threadId]).value;
		item->instParameter.push_back(cond);
		item->isConditionInst = true;
		item->brCondition = true;
		break;
	}

	default: {
		//			cerr << inst->getOpcodeName();
		//			assert(0 && "unsupport");
		break;
	}

	}
	//runtime.printRunTime();
	for (vector<Event*>::iterator ei = frontVirtualEvents.begin(), ee =
			frontVirtualEvents.end(); ei != ee; ei++) {
		trace->insertEvent(*ei, thread->threadId);
	}
	trace->insertEvent(item, thread->threadId);
	for (vector<Event*>::iterator ei = backVirtualEvents.begin(), ee =
			backVirtualEvents.end(); ei != ee; ei++) {
		trace->insertEvent(*ei, thread->threadId);
	}
	trace->insertPath(item);
	lastEvent = item;
}


//指令调用消息响应函数，在指令解释执行之后调用

void PSOListener::instructionExecuted(ExecutionState &state, KInstruction *ki) {
	if (lastEvent) {
		Instruction* inst = ki->inst;
		Thread* thread = state.currentThread;
		switch (inst->getOpcode()) {

		case Instruction::Switch: {
			BasicBlock* bb = thread->pc->inst->getParent();
			SwitchInst* si = dyn_cast<SwitchInst>(ki->inst);
			unsigned bbIndex = 0;
			bool isDefault = true;
			for (SwitchInst::CaseIt sci = si->case_begin(), sce =
					si->case_end(); sci != sce; sci++) {
				bbIndex++;
				if (sci.getCaseSuccessor() == bb) {
					isDefault = false;
					break;
				}
			}
			//对于switch语句的default块，index标记为-1
			if (isDefault) {
				bbIndex = 0;
			}
			break;
		}

		case Instruction::Ret: {
//			if (executor->isAllThreadTerminate()) {
//				for (map<uint64_t, Type*>::iterator mi =
//						usedGlobalVariableRecord.begin(), me =
//						usedGlobalVariableRecord.end(); mi != me; mi++) {
//					ObjectPair op;
//					//					cerr << mi->second->getTypeID() << endl;
//					ref<ConstantExpr> address = ConstantExpr::alloc(mi->first,
//							Context::get().getPointerWidth());
//					executor->getMemoryObject(op, state, address);
//					ref<Expr> value = getExprFromMemory(address, op,
//							mi->second);
//					rdManager.insertGlobalVariableLast(
//							createVarName(op.first->id, address, true),
//							expr2Constant(value.get(), mi->second));
//				}
//			}
			break;
		}

		case Instruction::Call: {

			break;
		}

		case Instruction::Load: {
			break;
		}

		case Instruction::Store: {
			break;
		}

		case Instruction::GetElementPtr: {
			break;
		}

		}
	}
}





//消息响应函数，在被测程序解释执行之后调用
void PSOListener::afterRunMethodAsMain() {
	Trace* trace = rdManager->getCurrentTrace();
	unsigned allGlobal = 0;
	if (executor->execStatus != Executor::SUCCESS) {
		cerr << "######################执行有错误,放弃本次执行####################\n";
		executor->isFinished = true;
		//		if (rdManager.getCurrentTrace()->traceType == Trace::FAILED) {
		//			cerr
		//					<< "######################错误来自于前缀#############################\n";
		//		} else {
		//			cerr
		//					<< "######################错误来自于执行#############################\n";
		//		}
	} else if (!rdManager->isCurrentTraceUntested()) {
		rdManager->getCurrentTrace()->traceType = Trace::REDUNDANT;
		cerr << "######################本条路径为旧路径####################\n";
		executor->getNewPrefix();
	} else {
		rdManager->runState = 1;
		std::map<std::string, std::vector<Event *> > &writeSet = trace->writeSet;
		std::map<std::string, std::vector<Event *> > &readSet = trace->readSet;
		for (std::map<std::string, std::vector<Event *> >::iterator nit =
				readSet.begin(), nie = readSet.end(); nit != nie; ++nit) {
			allGlobal += nit->second.size();
		}
		for (std::map<std::string, std::vector<Event *> >::iterator nit =
				writeSet.begin(), nie = writeSet.end(); nit != nie; ++nit) {
			std::string varName = nit->first;
			if (trace->readSet.find(varName) == trace->readSet.end()) {
				allGlobal += nit->second.size();
			}
		}
		rdManager->allGlobal += allGlobal;
	}

}


//消息相应函数，在前缀执行出错之后程序推出之前调用
void PSOListener::executionFailed(ExecutionState &state, KInstruction *ki) {
	rdManager->getCurrentTrace()->traceType = Trace::FAILED;
}


//消息相应函数，在创建了新线程之后调用
void PSOListener::createThread(ExecutionState &state, Thread* thread) {
	Trace* trace = rdManager->getCurrentTrace();
	trace->insertThreadCreateOrJoin(make_pair(lastEvent, thread->threadId),
			true);
}


//处理全局函数初始值
void PSOListener::handleInitializer(Constant* initializer, MemoryObject* mo,
		uint64_t& startAddress) {
	Trace* trace = rdManager->getCurrentTrace();
	DataLayout* layout = executor->kmodule->targetData;
	if (dyn_cast<ConstantInt>(initializer)) {
		Type* type = initializer->getType();
		unsigned alignment = layout->getABITypeAlignment(type);
		if (startAddress % alignment != 0) {
			startAddress = (startAddress / alignment + 1) * alignment;
		}
		string globalVariableName = createVarName(mo->id, startAddress,
				executor->isGlobalMO(mo));
		trace->insertGlobalVariableInitializer(globalVariableName, initializer);
//		cerr << "globalVariableName : " << globalVariableName << "    value : "
//				<< executor->evalConstant(initializer) << "\n";
		//startAddress += TypeUtils::getPrimitiveTypeWidth(type);
		startAddress += executor->kmodule->targetData->getTypeSizeInBits(type)
				/ 8;
	} else if (dyn_cast<ConstantFP>(initializer)) {
		Type* type = initializer->getType();
		unsigned alignment = layout->getABITypeAlignment(type);
		if (startAddress % alignment != 0) {
			startAddress = (startAddress / alignment + 1) * alignment;
		}
		string globalVariableName = createVarName(mo->id, startAddress,
				executor->isGlobalMO(mo));
		trace->insertGlobalVariableInitializer(globalVariableName, initializer);
//		cerr << "globalVariableName : " << globalVariableName << "    value : "
//				<< executor->evalConstant(initializer) << "\n";
		//startAddress += TypeUtils::getPrimitiveTypeWidth(type);
		startAddress += executor->kmodule->targetData->getTypeSizeInBits(type)
				/ 8;
	} else if (ConstantDataArray* carray = dyn_cast<ConstantDataArray>(
			initializer)) {
		ArrayType* arrayType = carray->getType();
		uint64_t elementNum = arrayType->getNumElements();
		for (unsigned index = 0; index < elementNum; index++) {
			Constant* element = carray->getAggregateElement(index);
			handleInitializer(element, mo, startAddress);
		}
	} else if (ConstantAggregateZero* caggregate = dyn_cast<
			ConstantAggregateZero>(initializer)) {
		uint64_t elementNum;
		switch (caggregate->getType()->getTypeID()) {

		case Type::StructTyID: {
			StructType* structType = dyn_cast<StructType>(
					caggregate->getType());
			if (structType->getStructName() == "union.pthread_mutex_t"
					|| structType->getStructName() == "union.pthread_cond_t"
					|| structType->getStructName()
							== "union.pthread_barrier_t") {
				unsigned alignment = layout->getABITypeAlignment(structType);
				if (startAddress % alignment != 0) {
					startAddress = (startAddress / alignment + 1) * alignment;
				}
				startAddress +=
						executor->kmodule->targetData->getTypeSizeInBits(
								structType) / 8;
			} else {
				elementNum = caggregate->getType()->getStructNumElements();
				for (unsigned index = 0; index < elementNum; index++) {
					Constant* element = caggregate->getAggregateElement(index);
					handleInitializer(element, mo, startAddress);
				}
			}

			break;
		}

		case Type::ArrayTyID: {
			elementNum = caggregate->getType()->getArrayNumElements();
			for (unsigned index = 0; index < elementNum; index++) {
				Constant* element = caggregate->getAggregateElement(index);
				handleInitializer(element, mo, startAddress);
			}
			break;
		}

		case Type::VectorTyID: {
			elementNum = caggregate->getType()->getVectorNumElements();
			for (unsigned index = 0; index < elementNum; index++) {
				Constant* element = caggregate->getAggregateElement(index);
				handleInitializer(element, mo, startAddress);
			}
			break;
		}

		default: {
			cerr << caggregate->getType()->getTypeID() << endl;
			assert(0 && "unknown aggregatezero type");
		}

		}
	} else if (ConstantStruct* cstruct = dyn_cast<ConstantStruct>(
			initializer)) {
		StructType* structType = cstruct->getType();
		if (structType->getStructName() == "union.pthread_mutex_t"
				|| structType->getStructName() == "union.pthread_cond_t"
				|| structType->getStructName() == "union.pthread_barrier_t") {
			unsigned alignment = layout->getABITypeAlignment(structType);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}
			startAddress += executor->kmodule->targetData->getTypeSizeInBits(
					structType) / 8;
		} else {
			uint64_t elementNum = structType->getNumElements();
			for (unsigned index = 0; index < elementNum; index++) {
				Constant* element = cstruct->getAggregateElement(index);
				handleInitializer(element, mo, startAddress);
			}
		}
	} else if (ConstantPointerNull* cpoint = dyn_cast<ConstantPointerNull>(
			initializer)) {
		Type* type = cpoint->getType();
		unsigned alignment = layout->getABITypeAlignment(type);
		if (startAddress % alignment != 0) {
			startAddress = (startAddress / alignment + 1) * alignment;
		}

		startAddress += executor->kmodule->targetData->getTypeSizeInBits(type)
				/ 8;
	} else if (llvm::ConstantExpr* cexpr = dyn_cast<llvm::ConstantExpr>(
			initializer)) {
		// handle global pointer which has been initialized. For example, char * a = "hello"
		handleConstantExpr(cexpr);
	} else if (ConstantArray * carray = dyn_cast<ConstantArray>(initializer)) {
		//handle array which has more than one dimension and initial value
		for (unsigned i = 0; i < carray->getNumOperands(); i++) {
			Constant* element = carray->getAggregateElement(i);
			handleInitializer(element, mo, startAddress);
		}
	} else {
		cerr << "value = " << initializer->getValueID() << " type = "
				<< initializer->getType()->getTypeID() << endl;
		assert(0 && "unsupported initializer");
	}
}

//处理LLVM::ConstantExpr类型
void PSOListener::handleConstantExpr(llvm::ConstantExpr* expr) {
	switch (expr->getOpcode()) {

	case Instruction::GetElementPtr: {
		GlobalVariable* op0 = dyn_cast<GlobalVariable>(expr->getOperand(0));
		Constant* trueInitializer = op0->getInitializer();
		MemoryObject *mo = executor->globalObjects.find(op0)->second;
		uint64_t address = mo->address;
		handleInitializer(trueInitializer, mo, address);
		break;
	}

	default: {
		cerr << expr->getOpcode() << endl;
		assert(0 && "unsupported Opcode");
	}

	}
}


//向全局变量表插入全局变量
void PSOListener::insertGlobalVariable(ref<Expr> address, Type* type) {
	ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address.get());
	uint64_t key = realAddress->getZExtValue();
	map<uint64_t, Type*>::iterator mi = usedGlobalVariableRecord.find(key);
	if (mi == usedGlobalVariableRecord.end()) {
		usedGlobalVariableRecord.insert(make_pair(key, type));
	}
}

//获取外部函数的返回值,必须在Call指令解释执行之后调用
Constant * PSOListener::handleFunctionReturnValue(ExecutionState & state,
		KInstruction * ki) {
	Instruction* inst = ki->inst;
	Function *f = lastEvent->calledFunction;
	Type* returnType = inst->getType();
	Constant* result = NULL;
	if (!f->getName().startswith("klee") && !executor->kmodule->functionMap[f]
			&& !returnType->isVoidTy()) {
		ref<Expr> returnValue = executor->getDestCell(state.currentStack, ki).value;
		result = Transfer::expr2Constant(returnValue.get(), returnType);
//		if (dyn_cast<ConstantInt>(result)) {
//			ConstantInt* ci = dyn_cast<ConstantInt>(result);
//			cerr << ci->getType()->getBitWidth() << " " << ci->getZExtValue() << endl;
//		} else if (dyn_cast<ConstantFP>(result)) {
//			ConstantFP* cfp = dyn_cast<ConstantFP>(result);
//			if (cfp->getType()->isFloatTy()) {
//				cerr << "float: " << cfp->getValueAPF().convertToFloat() << endl;
//			} else if (cfp->getType()->isDoubleTy()) {
//				cerr << "double: " << cfp->getValueAPF().convertToDouble() << endl;
//			}
//		}
	}
	return result;
}


//获取某些对指针参数指向空间进行写操作的外部函数的输入参数,必须在Call指令解释执行之后调用
void PSOListener::handleExternalFunction(ExecutionState& state,
		KInstruction *ki) {
	Trace* trace = rdManager->getCurrentTrace();
	Instruction* inst = ki->inst;
	Function *f = lastEvent->calledFunction;
	if (f->getName() == "strcpy") {

//		ref<Expr> scrAddress = executor->eval(ki, 2, state.currentstack[thread->threadId]).value;
//		ObjectPair scrop;
//		//处理scr
//		executor->getMemoryObject(scrop, state, scrAddress);
//
//		const MemoryObject* scrmo = scrop.first;
//		const ObjectState* scros = scrop.second;
//
//		ConstantExpr *caddress = cast<ConstantExpr>(scrAddress);
//		uint64_t scraddress = caddress->getZExtValue();
//		std::cerr << "dest" <<executor->isGlobalMO(scrmo)<< std::endl;
//		for (unsigned i = 0; i < scrmo->size; i++) {
//			std::cerr << "dest" << std::endl;
//			ref<Expr> ch = scros->read(i, 8);
//			Constant* constant = Transfer::expr2Constant(ch.get(),
//					Type::getInt8Ty(inst->getContext()));
//			ConstantExpr* cexpr = dyn_cast<ConstantExpr>(ch.get());
//			string name = createVarName(scrmo->id, scraddress + i,
//					executor->isGlobalMO(scrmo));
//			if (executor->isGlobalMO(scrmo)) {
//				unsigned loadTime = getLoadTime(scraddress + i);
//				trace->insertReadSet(name, lastEvent);
//				name = createGlobalVarFullName(name, loadTime, false);
//				lastEvent->isGlobal = true;
//			}
//			lastEvent->scrVariables.insert(make_pair(name, constant));
//			//cerr << "address = " << name << "value = " << ((ConstantInt*)constant)->getSExtValue() << endl;
//			//判断是否是字符串的末尾
//			if (cexpr->getZExtValue() == 0) {
//				break;
//			}
//		}
		ref<Expr> destAddress = executor->eval(ki, 1, state.currentStack).value;
		ObjectPair destop;
		//处理dest
		executor->getMemoryObject(destop, state, destAddress);
		const MemoryObject* destmo = destop.first;
		const ObjectState* destos = destop.second;
		ConstantExpr *caddress = cast<ConstantExpr>(destAddress);
		uint64_t destaddress = caddress->getZExtValue();
		for (unsigned i = 0; i < destmo->size - destaddress + destmo->address;
				i++) {
			ref<Expr> ch = destos->read(i, 8);
			ConstantExpr* cexpr = dyn_cast<ConstantExpr>(ch.get());
			string name = createVarName(destmo->id, destaddress + i,
					executor->isGlobalMO(destmo));
			if (executor->isGlobalMO(destmo)) {
				unsigned storeTime = getStoreTime(destaddress + i);

				name = createGlobalVarFullName(name, storeTime, true);

				lastEvent->isGlobal = true;
			}
#if DEBUGSTRCPY
			cerr << "Event name : " << lastEvent->eventName << "\n";
			cerr<<"name : "<<name<<std::endl;
#endif
			//cerr << "address = " << name << "value = " << ((ConstantInt*)constant)->getSExtValue() << endl;
			//判断是否是字符串的末尾
			if (cexpr->getZExtValue() == 0) {
				break;
			}
		}

	} else if (f->getName() == "getrlimit") {
		ref<Expr> address = executor->eval(ki, 2, state.currentStack).value;
		ObjectPair op;
		Type* type = inst->getOperand(1)->getType()->getPointerElementType();
		executor->getMemoryObject(op, state, address);
		uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
		analyzeInputValue(start, op, type);
	} else if (f->getName() == "lstat") {
		ref<Expr> address = executor->eval(ki, 2, state.currentStack).value;
		ObjectPair op;
		Type* type = inst->getOperand(1)->getType()->getPointerElementType();
		executor->getMemoryObject(op, state, address);
		uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
		analyzeInputValue(start, op, type);
	} else if (f->getName() == "time") {
		ref<Expr> address = executor->eval(ki, 1, state.currentStack).value;
		ObjectPair op;
		Type* type = inst->getOperand(0)->getType()->getPointerElementType();
		executor->getMemoryObject(op, state, address);
		uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
		analyzeInputValue(start, op, type);
	}

}


//解析一个MemoryObject，差分成每一个基本类型（Consant*）。对于指针不展开
void PSOListener::analyzeInputValue(uint64_t& address, ObjectPair& op,
		Type* type) {
	Trace* trace = rdManager->getCurrentTrace();
	DataLayout* layout = executor->kmodule->targetData;
	switch (type->getTypeID()) {
	case Type::HalfTyID:
	case Type::FloatTyID:
	case Type::DoubleTyID:
	case Type::X86_FP80TyID:
	case Type::FP128TyID:
	case Type::PPC_FP128TyID:
	case Type::IntegerTyID:
	case Type::PointerTyID: {
		const MemoryObject* mo = op.first;
		const ObjectState* os = op.second;
		//ABI or preferable
		//ylc
		unsigned alignment = layout->getABITypeAlignment(type);
		if (address % alignment != 0) {
			address = (address / alignment + 1) * alignment;
		}
		ref<Expr> value = os->read(address - mo->address,
				type->getPrimitiveSizeInBits());
		string variableName = createVarName(mo->id, address, executor->isGlobalMO(mo));
//		map<uint64_t, unsigned>::iterator index = storeRecord.find(address);
		unsigned storeTime = getStoreTime(address);
		address += type->getPrimitiveSizeInBits() / 8;
		if (executor->isGlobalMO(mo)) {
			variableName = createGlobalVarFullName(variableName, storeTime,
					true);
		}

//		if (constant->getType()->isIntegerTy()) {
//			cerr << variableName << " " << ((ConstantInt*)constant)->getSExtValue() << endl;
//		} else if (constant->getType()->isFloatTy()) {
//			cerr << variableName << " " << ((ConstantFP*)constant)->getValueAPF().convertToFloat() << endl;
//		} else if (constant->getType()->isDoubleTy()) {
//			cerr << variableName << " " << ((ConstantFP*)constant)->getValueAPF().convertToDouble() << endl;
//		}
		break;
	}

	case Type::StructTyID: {
		//opaque struct 无法解析
		assert(!dyn_cast<StructType>(type)->isOpaque());
		for (unsigned i = 0; i < type->getStructNumElements(); i++) {
			Type* elementType = type->getStructElementType(i);
			analyzeInputValue(address, op, elementType);
		}
		break;
	}

	case Type::ArrayTyID: {
		for (unsigned i = 0; i < type->getArrayNumElements(); i++) {
			Type* elementType = type->getArrayElementType();
			analyzeInputValue(address, op, elementType);
		}
		break;
	}

	case Type::VectorTyID: {
		for (unsigned i = 0; i < type->getVectorNumElements(); i++) {
			Type* elementType = type->getVectorElementType();
			analyzeInputValue(address, op, elementType);
		}
		break;
	}

	default: {
		cerr << "typeID: " << type->getTypeID() << endl;
		assert(0 && "unsupport type");
	}

	}
}


//计算全局变量的读操作次数
unsigned PSOListener::getLoadTime(uint64_t address) {
	unsigned loadTime;
	map<uint64_t, unsigned>::iterator index = loadRecord.find(address);
	if (index == loadRecord.end()) {
		loadRecord.insert(make_pair(address, 1));
		loadTime = 1;
	} else {
		loadTime = index->second + 1;
		loadRecord[address] = loadTime;
	}
	return loadTime;
}


//计算全局变量的写操作次数
unsigned PSOListener::getStoreTime(uint64_t address) {
	unsigned storeTime;
	map<uint64_t, unsigned>::iterator index = storeRecord.find(address);
	if (index == storeRecord.end()) {
		storeRecord.insert(make_pair(address, 1));
		storeTime = 1;
	} else {
		storeTime = index->second + 1;
		storeRecord[address] = storeTime;
	}
	return storeTime;
}

unsigned PSOListener::getStoreTimeForTaint(uint64_t address) {
	unsigned storeTime;
	map<uint64_t, unsigned>::iterator index = storeRecord.find(address);
	if (index == storeRecord.end()) {
		storeTime = 0;
	} else {
		storeTime = index->second;
	}
	return storeTime;
}


//获取函数指针指向的Function
Function * PSOListener::getPointeredFunction(ExecutionState & state,
		KInstruction * ki) {
	StackFrame* sf = &state.currentStack->realStack.back();
	//外部函数调用不会创建函数栈,其它函数调用会创建,此处需判断之前Call指令的执行是否已经创建了新的函数栈,
	//如果是,则倒数第二个元素是Call指令所在的函数栈.
	if (!ki->inst->getParent()->getParent()->getName().equals(
			sf->kf->function->getName())) {
		sf = &state.currentStack->realStack[state.currentStack->realStack.size() - 2];
	}
	int vnumber = ki->operands[0];
	ref<Expr> result;
	if (vnumber < 0) {
		unsigned index = -vnumber - 2;
		result = executor->kmodule->constantTable[index].value;
	} else {
		//cerr << "locals = " << sf->locals << " vnumber = " << vnumber << " function name = " << sf->kf->function->getName().str() << endl;
		result = sf->locals[vnumber].value;
	}
	ConstantExpr* addrExpr = dyn_cast<klee::ConstantExpr>(result);
	uint64_t addr = addrExpr->getZExtValue();
	return (Function*) addr;
}

}
