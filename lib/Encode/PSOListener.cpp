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
#include "../Core/Executor.h"
#include "../Core/ExternalDispatcher.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/DebugInfo.h"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <malloc.h>
#include <string>

using namespace std;
using namespace llvm;

#define PTR 0
#define DEBUGSTRCPY 0
#define COND_DEBUG 0

namespace klee {

	PSOListener::PSOListener(Executor* executor, RuntimeDataManager* rdManager) :
			BitcodeListener(rdManager), executor(executor), currentEvent(NULL) {
		// TODO Auto-generated constructor stub
		kind = PSOListenerKind;
	}

	PSOListener::~PSOListener() {
		// TODO Auto-generated destructor stub
		for (map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.begin(), bre = barrierRecord.end(); bri != bre; bri++) {
			if (bri->second) {
				delete bri->second;
			}
		}
	}

//消息响应函数，在被测程序解释执行之前调用
	void PSOListener::beforeRunMethodAsMain(ExecutionState &initialState) {

		Module* m = executor->kmodule->module;
		rdManager->createNewTrace(executor->executionNum);

		//收集全局变量初始化值
		for (Module::global_iterator i = m->global_begin(), e = m->global_end(); i != e; ++i) {
			if (i->hasInitializer() && i->getName().str().at(0) != '.') {
//				llvm::errs() << "name : " << i->getName().str() << "\n";
				MemoryObject *mo = executor->globalObjects.find(i)->second;
				Constant* initializer = i->getInitializer();
				uint64_t address = mo->address;
				handleInitializer(initializer, mo, address);
			}
		}

		unsigned traceNum = executor->executionNum;
		llvm::errs() << "\n";
		llvm::errs() << "************************************************************************\n";
		llvm::errs() << "第" << traceNum << "次执行,路径文件为trace" << traceNum << ".txt";
		if (traceNum == 1) {
			llvm::errs() << " 初始执行" << "\n";
		} else {
			llvm::errs() << " 前缀执行,前缀文件为prefix" << executor->prefix->getName() << ".txt" << "\n";
		}
		llvm::errs() << "************************************************************************\n";
		llvm::errs() << "\n";
	}

//指令调用消息响应函数，在指令解释执行前被调用
	void PSOListener::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		Instruction* inst = ki->inst;
		Thread* thread = state.currentThread;
		Event* item = NULL;
		KModule* kmodule = executor->kmodule;

//		llvm::errs() << "PSO thread id : " << thread->threadId;
//		inst->dump();

// filter the instruction linked by klee force-import such as klee_div_zero_check
		if (kmodule->internalFunctions.find(inst->getParent()->getParent()) == kmodule->internalFunctions.end()) {
			item = trace->createEvent(thread->threadId, ki, Event::NORMAL);
		} else {
			item = trace->createEvent(thread->threadId, ki, Event::IGNORE);
		}
//		llvm::errs() << "PSO event name : " << item->eventName << "\n";

		vector<Event*> frontVirtualEvents, backVirtualEvents; // the virtual event which should be inserted before/behind item
		frontVirtualEvents.reserve(1);
		backVirtualEvents.reserve(1);
		switch (inst->getOpcode()) {
			case Instruction::Call: {
//		inst->dump();
				CallSite cs(inst);
				Value *fp = cs.getCalledValue();
				Function *f = executor->getTargetFunction(fp, state);
				if (!f) {
					ref<Expr> expr = executor->eval(ki, 0, state).value;
					ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr);
					uint64_t functionPtr = constExpr->getZExtValue();
					f = (Function*) functionPtr;
				}
				item->calledFunction = f;
				if (f && f->isDeclaration() && f->getIntrinsicID() == Intrinsic::not_intrinsic
						&& kmodule->internalFunctions.find(f) == kmodule->internalFunctions.end()) {
					item->isFunctionWithSourceCode = false;
				}
//		llvm::errs()<<"isFunctionWithSourceCode : "<<item->isFunctionWithSourceCode<<"\n";

				//by hy
				//store all call arg
				if (!item->isFunctionWithSourceCode) {
					unsigned numArgs = cs.arg_size();
					item->instParameter.reserve(numArgs);
					for (unsigned j = 0; j < numArgs; ++j) {
						item->instParameter.push_back(executor->eval(ki, j + 1, state).value);
					}
				}

//		llvm::errs()<<"call name : "<< f->getName().str().c_str() <<"\n";
				if (f->getName().str() == "pthread_create") {
					ref<Expr> pthreadAddress = executor->eval(ki, 1, state).value;
					ObjectPair pthreadop;
					bool success = executor->getMemoryObject(pthreadop, state, state.currentStack->addressSpace, pthreadAddress);
					if (success) {
//				const ObjectState* pthreados = pthreadop.second;
						const MemoryObject* pthreadmo = pthreadop.first;
						ConstantExpr* realAddress = dyn_cast<ConstantExpr>(pthreadAddress);
						uint64_t key = realAddress->getZExtValue();
						if (executor->isGlobalMO(pthreadmo)) {
							item->isGlobal = true;
						}
						string varName = createVarName(pthreadmo->id, pthreadAddress, item->isGlobal);
						string varFullName;
						if (item->isGlobal) {
							unsigned loadTime = getLoadTime(key);
							varFullName = createGlobalVarFullName(varName, loadTime, false);
						}
						item->globalName = varFullName;
						item->name = varName;
					}
				} else if (f->getName().str() == "pthread_join") {
					CallInst* calli = dyn_cast<CallInst>(inst);
					IntegerType* paramType = (IntegerType*) (calli->getArgOperand(0)->getType());
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ConstantExpr* joinedThreadIdExpr = dyn_cast<ConstantExpr>(param);
					uint64_t joinedThreadId = joinedThreadIdExpr->getZExtValue(paramType->getBitWidth());
					trace->insertThreadCreateOrJoin(make_pair(item, joinedThreadId), false);
//					llvm::errs() << "event name : " << item->eventName << " joinedThreadId : " << param << "\n";
 				} else if (f->getName().str() == "pthread_cond_wait") {
					ref<Expr> param;
					ObjectPair op;
					Event *lock;
					bool success;
					param = executor->eval(ki, 2, state).value;
					success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						lock = trace->createEvent(thread->threadId, ki, Event::VIRTUAL);
						lock->calledFunction = f;
						backVirtualEvents.push_back(lock);
						trace->insertLockOrUnlock(thread->threadId, mutexName, item, false);
						trace->insertLockOrUnlock(thread->threadId, mutexName, lock, true);
					} else {
						assert(0 && "mutex not exist");
					}
					param = executor->eval(ki, 1, state).value;
					success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						trace->insertWait(condName, item, lock);
					} else {
						assert(0 && "cond not exist");
					}
#if COND_DEBUG
					ki->inst->dump();
					llvm::errs() << "event name : " << item->eventName << "\n";
					llvm::errs() << "wait : " << item->condName << "\n";
#endif
				} else if (f->getName().str() == "pthread_cond_signal") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						trace->insertSignal(condName, item);
					} else {
						assert(0 && "cond not exist");
					}
#if COND_DEBUG
					ki->inst->dump();
					llvm::errs() << "event name : " << item->eventName << "\n";
					llvm::errs() << "signal  : " << item->condName << "\n";
#endif
				} else if (f->getName().str() == "pthread_cond_broadcast") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string condName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						trace->insertSignal(condName, item);
					} else {
						assert(0 && "cond not exist");
					}
#if COND_DEBUG
					ki->inst->dump();
					llvm::errs() << "event name : " << item->eventName << "\n";
					llvm::errs() << "broadcast cond  : " << item->condName << "\n";
#endif
				} else if (f->getName().str() == "pthread_mutex_lock") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						trace->insertLockOrUnlock(thread->threadId, mutexName, item, true);
					} else {
						assert(0 && "mutex not exist");
					}
				} else if (f->getName().str() == "pthread_mutex_unlock") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, param);
					if (success) {
						const MemoryObject* mo = op.first;
						string mutexName = createVarName(mo->id, param, executor->isGlobalMO(mo));
						trace->insertLockOrUnlock(thread->threadId, mutexName, item, false);
					} else {
						assert(0 && "mutex not exist");
					}
				} else if (f->getName().str() == "pthread_barrier_wait") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ConstantExpr* barrierAddressExpr = dyn_cast<ConstantExpr>(param);
					uint64_t barrierAddress = barrierAddressExpr->getZExtValue();
					map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.find(barrierAddress);
					BarrierInfo* barrierInfo = NULL;
					if (bri == barrierRecord.end()) {
						barrierInfo = new BarrierInfo();
						barrierRecord.insert(make_pair(barrierAddress, barrierInfo));
					} else {
						barrierInfo = bri->second;
					}
					string barrierName = createBarrierName(barrierAddress, barrierInfo->releasedCount);
					trace->insertBarrierOperation(barrierName, item);
					//					llvm::errs() << "insert " << barrierName << " " << item->eventName << "\n";
					bool isReleased = barrierInfo->addWaitItem();
					if (isReleased) {
						barrierInfo->addReleaseItem();
					}
				} else if (f->getName().str() == "pthread_barrier_init") {
					ref<Expr> param = executor->eval(ki, 1, state).value;
					ConstantExpr* barrierAddressExpr = dyn_cast<ConstantExpr>(param);
					uint64_t barrierAddress = barrierAddressExpr->getZExtValue();
					map<uint64_t, BarrierInfo*>::iterator bri = barrierRecord.find(barrierAddress);
					BarrierInfo* barrierInfo = NULL;
					if (bri == barrierRecord.end()) {
						barrierInfo = new BarrierInfo();
						barrierRecord.insert(make_pair(barrierAddress, barrierInfo));
					} else {
						barrierInfo = bri->second;
					}

					param = executor->eval(ki, 3, state).value;
					ConstantExpr* countExpr = dyn_cast<ConstantExpr>(param);
					barrierInfo->count = countExpr->getZExtValue();
				} else if (f->getName() == "make_taint") {
					ref<Expr> address = executor->eval(ki, 1, state).value;
					ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address);
					if (realAddress) {
						uint64_t key = realAddress->getZExtValue();
						ObjectPair op;
						bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
						if (success) {
							const MemoryObject *mo = op.first;
							if (executor->isGlobalMO(mo)) {
								item->isGlobal = true;
							}
							string varName = createVarName(mo->id, address, item->isGlobal);
							string varFullName;
							if (item->isGlobal) {
								unsigned storeTime = getStoreTimeForTaint(key);
								if (storeTime == 0) {
									varFullName = varName + "_Init_tag";
								} else {
									varFullName = createGlobalVarFullName(varName, storeTime, true);
								}
							}
							item->globalName = varFullName;
							item->name = varName;
						}
					}
				} else if (kmodule->internalFunctions.find(f) != kmodule->internalFunctions.end()) {
					item->eventType = Event::IGNORE;
				}
				//llvm::errs() << item->calledFunction->getName().str() << " " << item->isUserDefinedFunction << "\n";
				break;
			}

			case Instruction::Ret: {
				break;
			}

			case Instruction::GetElementPtr: {
				//对于对数组解引用的GetElementPtr，获取其所有元素的地址，存放在Event中
				//目前只处理一维数组,多维数组及指针数组赞不考虑，该处理函数存在问题
				//ylc
				KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
				for (std::vector<std::pair<unsigned, uint64_t> >::iterator it = kgepi->indices.begin(), ie = kgepi->indices.end(); it != ie;
						++it) {
					ref<Expr> index = executor->eval(ki, it->first, state).value;
//			llvm::errs() << "kgepi->index : " << index << "\n";
					item->instParameter.push_back(index);
				}
				if (kgepi->offset) {
					item->instParameter.push_back(ConstantExpr::create(kgepi->offset, 64));
				}
				item->isConditionInst = true;
				item->brCondition = true;
				break;
			}
			case Instruction::Br: {
				BranchInst *bi = dyn_cast<BranchInst>(inst);
				if (!bi->isUnconditional()) {
					item->isConditionInst = true;
					ref<Expr> param = executor->eval(ki, 0, state).value;
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
				if (li->getPointerOperand()->getName().equals("stdout") || li->getPointerOperand()->getName().equals("stderr")) {
					item->eventType = Event::IGNORE;
				} else {
					ref<Expr> address = executor->eval(ki, 0, state).value;
					ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address);
					if (realAddress) {
						uint64_t key = realAddress->getZExtValue();
						ObjectPair op;
						bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
						if (success) {
							const MemoryObject *mo = op.first;
							if (executor->isGlobalMO(mo)) {
								item->isGlobal = true;
							}
							string varName = createVarName(mo->id, address, item->isGlobal);
							string varFullName;
							if (item->isGlobal) {
								unsigned loadTime = getLoadTime(key);
								varFullName = createGlobalVarFullName(varName, loadTime, false);
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
							if (inst->getOperand(0)->getValueID() == Value::InstructionVal + Instruction::GetElementPtr) {

							}
						} else {
							llvm::errs() << "Load address = " << realAddress->getZExtValue() << "\n";
							assert(0 && "load resolve unsuccess");
						}
					} else {
						assert(0 && " address is not const");
					}
				}
				break;
			}

			case Instruction::Store: {
				ref<Expr> value = executor->eval(ki, 0, state).value;
				item->instParameter.push_back(value);
				ConstantExpr* realValue = dyn_cast<ConstantExpr>(value);
				if (realValue) {
					Type* valueTy = ki->inst->getOperand(0)->getType();
					if (valueTy->isPointerTy()) {
//						llvm::errs() << "valueTy->isPointerTy()\n";
						uint64_t startAddress = realValue->getZExtValue();
						valueTy = valueTy->getPointerElementType();
						executor->createSpecialElement(state, valueTy, startAddress, false);
					}
				}
//				llvm::errs() << "PSO Store\n";
				ref<Expr> address = executor->eval(ki, 1, state).value;
				ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address);
//				llvm::errs() << "address : ";
//				address->dump();
				if (realAddress) {
					uint64_t key = realAddress->getZExtValue();
//					llvm::errs() << "key : " << key << "\n";
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
					if (success) {
						const MemoryObject *mo = op.first;
						if (executor->isGlobalMO(mo)) {
							item->isGlobal = true;
						}
						string varName = createVarName(mo->id, address, item->isGlobal);
						string varFullName;
						if (item->isGlobal) {
							unsigned storeTime = getStoreTime(key);
							varFullName = createGlobalVarFullName(varName, storeTime, true);
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
					} else {
						llvm::errs() << "Store address = " << realAddress->getZExtValue() << "\n";
						assert(0 && "store resolve unsuccess");
					}
				} else {
					assert(0 && " address is not const");
				}
				break;
			}
			case Instruction::Switch: {
				ref<Expr> cond = executor->eval(ki, 0, state).value;
				item->instParameter.push_back(cond);
				item->isConditionInst = true;
				item->brCondition = true;
				break;
			}

			default: {
				break;
			}

		}
		for (vector<Event*>::iterator ei = frontVirtualEvents.begin(), ee = frontVirtualEvents.end(); ei != ee; ei++) {
			trace->insertEvent(*ei, thread->threadId);
		}
		trace->insertEvent(item, thread->threadId);
		for (vector<Event*>::iterator ei = backVirtualEvents.begin(), ee = backVirtualEvents.end(); ei != ee; ei++) {
			trace->insertEvent(*ei, thread->threadId);
		}
		trace->insertPath(item);
		currentEvent = item;
	}

//指令调用消息响应函数，在指令解释执行之后调用
	void PSOListener::afterExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		Instruction* inst = ki->inst;

		switch (inst->getOpcode()) {
			case Instruction::Call: {
//		inst->dump();
				CallSite cs(inst);
				Value *fp = cs.getCalledValue();
				Function *f = executor->getTargetFunction(fp, state);
				if (!f) {
					ref<Expr> expr = executor->eval(ki, 0, state).value;
					ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr);
					uint64_t functionPtr = constExpr->getZExtValue();
					f = (Function*) functionPtr;
				}

//		llvm::errs()<<"call name : "<< f->getName().str().c_str() <<"\n";
				if (f->getName().str() == "pthread_create") {
					ref<Expr> pthreadAddress = executor->eval(ki, 1, state).value;
					Expr::Width type = executor->getWidthForLLVMType(inst->getType());
					ref<Expr> pid = executor->readExpr(state, state.currentThread->stack->addressSpace, pthreadAddress, type);
					ConstantExpr* pidConstant = dyn_cast<ConstantExpr>(pid);
					uint64_t pidInt = pidConstant->getZExtValue();
					trace->insertThreadCreateOrJoin(make_pair(currentEvent, pidInt), true);
//					llvm::errs() << "PSO pthread_create event name : " << currentEvent->eventName << " pid : " << pid << "\n";
				}
				break;
			}
			case Instruction::Load: {
				ref<Expr> value = executor->getDestCell(state, ki).value;
//				llvm::errs() << "PSO load : " << value << "\n";
				break;
			}

			default: {
				break;
			}
		}
	}

//消息响应函数，在被测程序解释执行之后调用
	void PSOListener::afterRunMethodAsMain(ExecutionState &state) {
	}

//消息相应函数，在前缀执行出错之后程序推出之前调用
	void PSOListener::executionFailed(ExecutionState &state, KInstruction *ki) {
		rdManager->getCurrentTrace()->traceType = Trace::FAILED;
	}

//处理全局函数初始值
	void PSOListener::handleInitializer(Constant* initializer, MemoryObject* mo, uint64_t& startAddress) {
		Trace* trace = rdManager->getCurrentTrace();
		DataLayout* layout = executor->kmodule->targetData;
		if (dyn_cast<ConstantInt>(initializer)) {
			Type* type = initializer->getType();
			unsigned alignment = layout->getABITypeAlignment(type);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}
			string globalVariableName = createVarName(mo->id, startAddress, executor->isGlobalMO(mo));
			trace->insertGlobalVariableInitializer(globalVariableName, initializer);
//		llvm::errs() << "globalVariableName : " << globalVariableName << "    value : "
//				<< executor->evalConstant(initializer) << "\n";
			//startAddress += TypeUtils::getPrimitiveTypeWidth(type);
			startAddress += executor->kmodule->targetData->getTypeSizeInBits(type) / 8;
		} else if (dyn_cast<ConstantFP>(initializer)) {
			Type* type = initializer->getType();
			unsigned alignment = layout->getABITypeAlignment(type);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}
			string globalVariableName = createVarName(mo->id, startAddress, executor->isGlobalMO(mo));
			trace->insertGlobalVariableInitializer(globalVariableName, initializer);
//		llvm::errs() << "globalVariableName : " << globalVariableName << "    value : "
//				<< executor->evalConstant(initializer) << "\n";
			//startAddress += TypeUtils::getPrimitiveTypeWidth(type);
			startAddress += executor->kmodule->targetData->getTypeSizeInBits(type) / 8;
		} else if (ConstantDataArray* carray = dyn_cast<ConstantDataArray>(initializer)) {
			ArrayType* arrayType = carray->getType();
			uint64_t elementNum = arrayType->getNumElements();
			for (unsigned index = 0; index < elementNum; index++) {
				Constant* element = carray->getAggregateElement(index);
				handleInitializer(element, mo, startAddress);
			}
		} else if (ConstantAggregateZero* caggregate = dyn_cast<ConstantAggregateZero>(initializer)) {
			uint64_t elementNum;
			switch (caggregate->getType()->getTypeID()) {

				case Type::StructTyID: {
					StructType* structType = dyn_cast<StructType>(caggregate->getType());
					if (!structType->isLiteral()) {
						if (structType->getStructName() == "union.pthread_mutex_t" || structType->getStructName() == "union.pthread_cond_t"
								|| structType->getStructName() == "union.pthread_barrier_t") {
							unsigned alignment = layout->getABITypeAlignment(structType);
							if (startAddress % alignment != 0) {
								startAddress = (startAddress / alignment + 1) * alignment;
							}
							startAddress += executor->kmodule->targetData->getTypeSizeInBits(structType) / 8;
						}

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
					llvm::errs() << caggregate->getType()->getTypeID() << "\n";
					assert(0 && "unknown aggregatezero type");
				}

			}
		} else if (ConstantStruct* cstruct = dyn_cast<ConstantStruct>(initializer)) {
			StructType* structType = cstruct->getType();
			if (!structType->isLiteral()) {
				if (structType->getStructName() == "union.pthread_mutex_t" || structType->getStructName() == "union.pthread_cond_t"
						|| structType->getStructName() == "union.pthread_barrier_t") {
					unsigned alignment = layout->getABITypeAlignment(structType);
					if (startAddress % alignment != 0) {
						startAddress = (startAddress / alignment + 1) * alignment;
					}
					startAddress += executor->kmodule->targetData->getTypeSizeInBits(structType) / 8;
				}
			} else {
				uint64_t elementNum = structType->getNumElements();
				for (unsigned index = 0; index < elementNum; index++) {
					Constant* element = cstruct->getAggregateElement(index);
					handleInitializer(element, mo, startAddress);
				}
			}
		} else if (ConstantPointerNull* cpoint = dyn_cast<ConstantPointerNull>(initializer)) {
			Type* type = cpoint->getType();
			unsigned alignment = layout->getABITypeAlignment(type);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}

			startAddress += executor->kmodule->targetData->getTypeSizeInBits(type) / 8;
		} else if (llvm::ConstantExpr* cexpr = dyn_cast<llvm::ConstantExpr>(initializer)) {
			// handle global pointer which has been initialized. For example, char * a = "hello"
			handleConstantExpr(cexpr);
		} else if (ConstantArray * carray = dyn_cast<ConstantArray>(initializer)) {
			//handle array which has more than one dimension and initial value
			for (unsigned i = 0; i < carray->getNumOperands(); i++) {
				Constant* element = carray->getAggregateElement(i);
				handleInitializer(element, mo, startAddress);
			}
		} else {
//			llvm::errs() << "value = " << initializer->getValueID() << " type = " << initializer->getType()->getTypeID() << "\n";
//			assert(0 && "unsupported initializer");
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
//				llvm::errs() << expr->getOpcode() << "\n";
//				assert(0 && "unsupported Opcode");
			}

		}
	}

//向全局变量表插入全局变量
	void PSOListener::insertGlobalVariable(ref<Expr> address, Type* type) {
		ConstantExpr* realAddress = dyn_cast<ConstantExpr>(address);
		uint64_t key = realAddress->getZExtValue();
		map<uint64_t, Type*>::iterator mi = usedGlobalVariableRecord.find(key);
		if (mi == usedGlobalVariableRecord.end()) {
			usedGlobalVariableRecord.insert(make_pair(key, type));
		}
	}

//获取外部函数的返回值,必须在Call指令解释执行之后调用
	Constant * PSOListener::handleFunctionReturnValue(ExecutionState & state, KInstruction * ki) {
		Instruction* inst = ki->inst;
		Function *f = currentEvent->calledFunction;
		Type* returnType = inst->getType();
		Constant* result = NULL;
		if (!f->getName().startswith("klee") && !executor->kmodule->functionMap[f] && !returnType->isVoidTy()) {
			ref<Expr> returnValue = executor->getDestCell(state, ki).value;
			result = Transfer::expr2Constant(returnValue.get(), returnType);
		}
		return result;
	}

//获取某些对指针参数指向空间进行写操作的外部函数的输入参数,必须在Call指令解释执行之后调用
	void PSOListener::handleExternalFunction(ExecutionState& state, KInstruction *ki) {
//	Trace* trace = rdManager->getCurrentTrace();
		Instruction* inst = ki->inst;
		Function *f = currentEvent->calledFunction;
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
//		llvm::errs() << "dest" <<executor->isGlobalMO(scrmo)<< "\n";
//		for (unsigned i = 0; i < scrmo->size; i++) {
//			llvm::errs() << "dest" << "\n";
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
//			//llvm::errs() << "address = " << name << "value = " << ((ConstantInt*)constant)->getSExtValue() << "\n";
//			//判断是否是字符串的末尾
//			if (cexpr->getZExtValue() == 0) {
//				break;
//			}
//		}
			ref<Expr> destAddress = executor->eval(ki, 1, state).value;
			ObjectPair destop;
			//处理dest
			executor->getMemoryObject(destop, state, state.currentStack->addressSpace, destAddress);
			const MemoryObject* destmo = destop.first;
			const ObjectState* destos = destop.second;
			ConstantExpr *caddress = cast<ConstantExpr>(destAddress);
			uint64_t destaddress = caddress->getZExtValue();
			for (unsigned i = 0; i < destmo->size - destaddress + destmo->address; i++) {
				ref<Expr> ch = destos->read(i, 8);
				ConstantExpr* cexpr = dyn_cast<ConstantExpr>(ch);
				string name = createVarName(destmo->id, destaddress + i, executor->isGlobalMO(destmo));
				if (executor->isGlobalMO(destmo)) {
					unsigned storeTime = getStoreTime(destaddress + i);

					name = createGlobalVarFullName(name, storeTime, true);

					currentEvent->isGlobal = true;
				}
#if DEBUGSTRCPY
				llvm::errs() << "Event name : " << currentEvent->eventName << "\n";
				llvm::errs()<<"name : "<<name<<"\n";
#endif
				//llvm::errs() << "address = " << name << "value = " << ((ConstantInt*)constant)->getSExtValue() << "\n";
				//判断是否是字符串的末尾
				if (cexpr->getZExtValue() == 0) {
					break;
				}
			}

		} else if (f->getName() == "getrlimit") {
			ref<Expr> address = executor->eval(ki, 2, state).value;
			ObjectPair op;
			Type* type = inst->getOperand(1)->getType()->getPointerElementType();
			executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
			uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
			analyzeInputValue(start, op, type);
		} else if (f->getName() == "lstat") {
			ref<Expr> address = executor->eval(ki, 2, state).value;
			ObjectPair op;
			Type* type = inst->getOperand(1)->getType()->getPointerElementType();
			executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
			uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
			analyzeInputValue(start, op, type);
		} else if (f->getName() == "time") {
			ref<Expr> address = executor->eval(ki, 1, state).value;
			ObjectPair op;
			Type* type = inst->getOperand(0)->getType()->getPointerElementType();
			executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
			uint64_t start = dyn_cast<ConstantExpr>(address)->getZExtValue();
			analyzeInputValue(start, op, type);
		}

	}

//解析一个MemoryObject，差分成每一个基本类型（Consant*）。对于指针不展开
	void PSOListener::analyzeInputValue(uint64_t& address, ObjectPair& op, Type* type) {
//	Trace* trace = rdManager->getCurrentTrace();
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
				ref<Expr> value = os->read(address - mo->address, type->getPrimitiveSizeInBits());
				string variableName = createVarName(mo->id, address, executor->isGlobalMO(mo));
//		map<uint64_t, unsigned>::iterator index = storeRecord.find(address);
				unsigned storeTime = getStoreTime(address);
				address += type->getPrimitiveSizeInBits() / 8;
				if (executor->isGlobalMO(mo)) {
					variableName = createGlobalVarFullName(variableName, storeTime, true);
				}

//		if (constant->getType()->isIntegerTy()) {
//			llvm::errs() << variableName << " " << ((ConstantInt*)constant)->getSExtValue() << "\n";
//		} else if (constant->getType()->isFloatTy()) {
//			llvm::errs() << variableName << " " << ((ConstantFP*)constant)->getValueAPF().convertToFloat() << "\n";
//		} else if (constant->getType()->isDoubleTy()) {
//			llvm::errs() << variableName << " " << ((ConstantFP*)constant)->getValueAPF().convertToDouble() << "\n";
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
				llvm::errs() << "typeID: " << type->getTypeID() << "\n";
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
	Function * PSOListener::getPointeredFunction(ExecutionState & state, KInstruction * ki) {
		StackFrame* sf = &state.currentStack->realStack.back();
		//外部函数调用不会创建函数栈,其它函数调用会创建,此处需判断之前Call指令的执行是否已经创建了新的函数栈,
		//如果是,则倒数第二个元素是Call指令所在的函数栈.
		if (!ki->inst->getParent()->getParent()->getName().equals(sf->kf->function->getName())) {
			sf = &state.currentStack->realStack[state.currentStack->realStack.size() - 2];
		}
		int vnumber = ki->operands[0];
		ref<Expr> result;
		if (vnumber < 0) {
			unsigned index = -vnumber - 2;
			result = executor->kmodule->constantTable[index].value;
		} else {
			//llvm::errs() << "locals = " << sf->locals << " vnumber = " << vnumber << " function name = " << sf->kf->function->getName().str() << "\n";
			result = sf->locals[vnumber].value;
		}
		ConstantExpr* addrExpr = dyn_cast<klee::ConstantExpr>(result);
		uint64_t addr = addrExpr->getZExtValue();
		return (Function*) addr;
	}

}
