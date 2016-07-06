/*
 * SymbolicListener.cpp
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#include "SymbolicListener.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/Casting.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <utility>

#include "../../include/klee/Internal/Module/Cell.h"
#include "../../include/klee/Internal/Module/InstructionInfoTable.h"
#include "../../include/klee/Internal/Module/KModule.h"
#include "../Core/Memory.h"
#include "../Thread/StackFrame.h"
#include "Trace.h"
#include "../Core/Executor.h"


using namespace std;
using namespace llvm;

#define EVENTS_DEBUG 0
#define PTR 0
#define DEBUGSTRCPY 0
#define DEBUGSYMBOLIC 0
#define COND_DEBUG 0
#define BIT_WIDTH 64
#define POINT_BIT_WIDTH 64

namespace klee {

SymbolicListener::SymbolicListener(Executor* executor, RuntimeDataManager* rdManager) :
		BitcodeListener(), executor(executor), runtimeData(rdManager) {
	kind = SymbolicListenerKind;
	kleeBr = false;
}

SymbolicListener::~SymbolicListener() {

}

//消息响应函数，在被测程序解释执行之前调用
void SymbolicListener::beforeRunMethodAsMain(ExecutionState &initialState) {

	//收集全局变量初始化值
	Trace* trace = runtimeData->getCurrentTrace();
	currentEvent = trace->path.begin();
	endEvent = trace->path.end();
	//收集assert
	for (std::vector<KFunction*>::iterator i =
			executor->kmodule->functions.begin(), e =
			executor->kmodule->functions.end(); i != e; ++i) {
		KInstruction **instructions = (*i)->instructions;
		for (unsigned j = 0; j < (*i)->numInstructions; j++) {
			KInstruction *ki = instructions[j];
			Instruction* inst = ki->inst;
//			instructions[j]->inst->dump();
			if (inst->getOpcode() == Instruction::Call) {
				CallSite cs(inst);
				Value *fp = cs.getCalledValue();
				Function *f = executor->getTargetFunction(fp, initialState);
				if (f && f->getName().str() == "__assert_fail") {
					string fileName = ki->info->file;
					unsigned line = ki->info->line;
					assertMap[fileName].push_back(line);
//					printf("fileName : %s, line : %d\n",fileName.c_str(),line);
//					std::cerr << "call name : " << f->getName().str() << "\n";
				}
			}
		}
	}
}


void SymbolicListener::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
	Trace* trace = runtimeData->getCurrentTrace();
	if ((*currentEvent)) {
		Instruction* inst = ki->inst;
//		Thread* thread = state.currentThread;
//		cerr << "event name : " << (*currentEvent)->eventName << " ";
//		cerr << "thread
//		cerr << "thread id : " << (*currentEvent)->threadId ;
//		(*currentEvent)->inst->inst->dump();
		switch (inst->getOpcode()) {
		case Instruction::Load: {
			ref<Expr> address = executor->eval(ki, 0, state).value;
			if (address->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getGlobalName(address)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "load symbolic print");
				}
				executor->ineval(ki, 0, state, value);
			}
			break;
		}
		case Instruction::Store: {
			ref<Expr> address = executor->eval(ki, 1, state).value;
			if (address->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getGlobalName(address)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "store address is symbolic");
				}
				executor->ineval(ki, 1, state, value);
			}

			ref<Expr> value = executor->eval(ki, 0, state).value;
			ref<Expr> base = executor->eval(ki, 1, state).value;
			Type::TypeID id = ki->inst->getOperand(0)->getType()->getTypeID();
//			cerr << "store value : " << value << std::endl;
//			cerr << "store base : " << base << std::endl;
//			cerr << "value->getKind() : " << value->getKind() << std::endl;
//			cerr << "TypeID id : " << id << std::endl;
			bool isFloat = 0;
			if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
				isFloat = 1;
			}
			if ((*currentEvent)->isGlobal) {
				if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
					Expr::Width size = executor->getWidthForLLVMType(ki->inst->getOperand(0)->getType());
					ref<Expr> address = executor->eval(ki, 1, state).value;
					ref<Expr> symbolic = manualMakeSymbolic(state,
							(*currentEvent)->globalName, size, isFloat);
					ref<Expr> constraint = EqExpr::create(value, symbolic);
					trace->storeSymbolicExpr.push_back(constraint);
//					cerr << "event name : " << (*currentEvent)->eventName << "\n";
//					cerr << "store constraint : " << constraint << "\n";
					if (value->getKind() == Expr::Constant) {

					} else if (value->getKind() == Expr::Concat || value->getKind() == Expr::Read) {
						ref<Expr> svalue = symbolicMap[filter.getGlobalName(value)];
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic");
						} else if (id == Type::PointerTyID && value->getKind() == Expr::Read) {
							assert (0 && "pointer is expr::read");
						}
						executor->ineval(ki, 0, state, svalue);
					} else {
						ref<Expr> svalue = (*currentEvent)->instParameter.back();
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic");
						} else 	if (id == Type::PointerTyID) {
							assert (0 && "pointer is other symbolic");
						}
						executor->ineval(ki, 0, state, svalue);
					}
				} else {
					if (value->getKind() != Expr::Constant) {
						assert(0 && "store value is symbolic and type is other");
					}
				}
			} else {
				//会丢失指针的一些关系约束，但是不影响。
				if (id == Type::PointerTyID && PTR) {
					if (value->getKind() == Expr::Concat){
						ref<Expr> svalue = symbolicMap[filter.getGlobalName(value)];
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store pointer is symbolic");
						}
						executor->ineval(ki, 0, state, svalue);
						ref<Expr> address = executor->eval(ki, 1, state).value;
						addressSymbolicMap[address] = value;
//						cerr << "address : " << address << " value : " << value << "\n";
					} else if (value->getKind() == Expr::Read) {
						assert (0 && "pointer is expr::read");
					} else {
						ref<Expr> address = executor->eval(ki, 1, state).value;
						addressSymbolicMap[address] = value;
//						cerr << "address : " << address << " value : " << value << "\n";
					}
				} else if (isFloat || id == Type::IntegerTyID) {
					//局部非指针变量内存中可能存储符号值。
				} else {
					if (value->getKind() != Expr::Constant) {
						assert(0 && "store value is symbolic and type is other");
					}
				}
			}
			break;
		}
		case Instruction::Br: {
			BranchInst *bi = dyn_cast<BranchInst>(inst);
			if (!bi->isUnconditional()) {
				unsigned isAssert = 0;
				string fileName = ki->info->file;
				std::map<string, std::vector<unsigned> >::iterator it =
						assertMap.find(fileName);
				unsigned line = ki->info->line;
				if (it != assertMap.end()) {
					if (find(assertMap[fileName].begin(), assertMap[fileName].end(), line)
							!= assertMap[fileName].end()) {
						isAssert = 1;
					}
				}
				ref<Expr> value1 = executor->eval(ki, 0, state).value;
				if (value1->getKind() != Expr::Constant) {
					Expr::Width width = value1->getWidth();
					ref<Expr> value2;
					if ((*currentEvent)->brCondition == true) {
						value2 = ConstantExpr::create(true, width);
					} else {
						value2 = ConstantExpr::create(false, width);
					}
					ref<Expr> constraint = EqExpr::create(value1, value2);
					if (isAssert) {
//						cerr << "event name : " << (*currentEvent)->eventName << "\n";
//						cerr << "assert constraint : " << constraint << "\n";
						trace->assertSymbolicExpr.push_back(constraint);
						trace->assertEvent.push_back((*currentEvent));
					} else if (kleeBr == false) {
//						cerr << "event name : " << (*currentEvent)->eventName << "\n";
//						cerr << "br constraint : " << constraint << "\n";
						trace->brSymbolicExpr.push_back(constraint);
						trace->brEvent.push_back((*currentEvent));
					}
					executor->ineval(ki, 0, state, value2);
				}
				if (kleeBr == true) {
					kleeBr = false;
				}
			}
			break;
		}
		case Instruction::Select: {

			break;
		}
		case Instruction::Call: {
			CallSite cs(inst);
			Function *f = (*currentEvent)->calledFunction;
			ref<Expr> function = executor->eval(ki, 0, state).value;
			if (function->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getGlobalName(function)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "call function is symbolic");
				}
				executor->ineval(ki, 0, state, value);
			}
//			std::cerr<<"isFunctionWithSourceCode : ";
//					(*currentEvent)->inst->inst->dump();
//			std::cerr<<"isFunctionWithSourceCode : ";
//					inst->dump();
//			std::cerr<<"isFunctionWithSourceCode : "<<(*currentEvent)->isFunctionWithSourceCode<<"\n";
			if (!(*currentEvent)->isFunctionWithSourceCode) {
				unsigned numArgs = cs.arg_size();
				for (unsigned j = numArgs; j > 0; j--) {
					ref<Expr> value = executor->eval(ki, j, state).value;
					Type::TypeID id = cs.getArgument(j-1)->getType()->getTypeID();
//					cerr << "value->getKind() : " << value->getKind() << std::endl;
//					cerr << "TypeID id : " << id << std::endl;
//		    		cerr<<"value : " << value << "\n";
					bool isFloat = 0;
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
						if (value->getKind() == Expr::Constant) {

						} else if (value->getKind() == Expr::Concat || value->getKind() == Expr::Read) {
							ref<Expr> svalue = symbolicMap[filter.getGlobalName(value)];
							if (svalue->getKind() != Expr::Constant) {
								assert(0 && "store value is symbolic");
							} else if (id == Type::PointerTyID && value->getKind() == Expr::Read) {
								assert (0 && "pointer is expr::read");
							}
							executor->ineval(ki, j, state, svalue);
						} else {
							ref<Expr> svalue = (*currentEvent)->instParameter[j-1];
							if (svalue->getKind() != Expr::Constant) {
								assert(0 && "store value is symbolic");
							} else 	if (id == Type::PointerTyID) {
								if (f->getName().str() == "pthread_create") {

								} else {
									assert (0 && "pointer is other symbolic");
								}
							}
							executor->ineval(ki, j, state, svalue);
						}
					} else {
						if (value->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic and type is other");
						}
					}
				}
			}
			break;
		}
		case Instruction::GetElementPtr: {
			KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
			ref<Expr> base = executor->eval(ki, 0, state).value;
			if (base->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getGlobalName(base)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "GetElementPtr symbolic print");
				}
				executor->ineval(ki, 0, state, value);
			} else if (base->getKind() == Expr::Read) {
				assert (0 && "pointer is expr::read");
			}
//			std::cerr << "kgepi->base : " << base << std::endl;
			std::vector<ref<klee::Expr> >::iterator first = (*currentEvent)->instParameter.begin();
			for (std::vector<std::pair<unsigned, uint64_t> >::iterator
					it = kgepi->indices.begin(), ie = kgepi->indices.end();
					it != ie; ++it) {
				ref<Expr> index = executor->eval(ki, it->first, state).value;
//				std::cerr << "kgepi->index : " << index << std::endl;
//				std::cerr << "first : " << *first << std::endl;
				if (index->getKind() != Expr::Constant) {
					executor->ineval(ki, it->first, state, *first);
					ref<Expr> constraint = EqExpr::create(index, *first);
//					cerr << "event name : " << (*currentEvent)->eventName << "\n";
//					cerr << "constraint : " << constraint << "\n";
					trace->brSymbolicExpr.push_back(constraint);
					trace->brEvent.push_back((*currentEvent));
				} else {
					if (index != *first) {
						assert(0 && "index != first");
					}
				}
				++first;
			}
			if (kgepi->offset) {
//				std::cerr<<"kgepi->offset : "<<kgepi->offset<<std::endl;
				//目前没有这种情况...
//				assert(0 &&"kgepi->offset");
			}
			break;
		}
		case Instruction::Switch: {
//			SwitchInst *si = cast<SwitchInst>(inst);
			ref<Expr> cond1 = executor->eval(ki, 0, state).value;
			if (cond1->getKind() != Expr::Constant) {
				ref<Expr> cond2 = (*currentEvent)->instParameter.back();
				ref<Expr> constraint = EqExpr::create(cond1, cond2);
				trace->brSymbolicExpr.push_back(constraint);
				trace->brEvent.push_back((*currentEvent));
				executor->ineval(ki, 0, state, cond2);
			}
			break;
		}
		case Instruction::PtrToInt: {
//			CastInst *ci = cast<CastInst>(inst);
//			Expr::Width iType = executor->getWidthForLLVMType(ci->getType());
			ref<Expr> arg = executor->eval(ki, 0, state).value;
			if (arg->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getGlobalName(arg)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "GetElementPtr symbolic print");
				}
				executor->ineval(ki, 0, state, value);
			} else if (arg->getKind() == Expr::Read) {
				assert (0 && "pointer is expr::read");
			}
			break;
		}
		default: {
			break;
		}
		}
	}

}

void SymbolicListener::afterExecuteInstruction(ExecutionState &state, KInstruction *ki) {
	Trace* trace = runtimeData->getCurrentTrace();
	if ((*currentEvent)) {
		Instruction* inst = ki->inst;
		Thread* thread = state.currentThread;
		switch (inst->getOpcode()) {
		case Instruction::Load: {
			ref<Expr> value = executor->getDestCell(state, ki).value;
//			cerr << "value : " << value << "\n";
			bool isFloat = 0;
			Type::TypeID id = ki->inst->getType()->getTypeID();
			if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
				isFloat = 1;
			}
			if ((*currentEvent)->isGlobal) {

				//指针！！！
#if PTR
				if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
				if (isFloat || id == Type::IntegerTyID) {
#endif

					Expr::Width size = executor->getWidthForLLVMType(ki->inst->getType());
					ref<Expr> address = executor->eval(ki, 0, state).value;
					ref<Expr> value = executor->getDestCell(state, ki).value;
					ref<Expr> symbolic = manualMakeSymbolic(state,
							(*currentEvent)->globalName, size, isFloat);
					executor->bindLocal(ki, state, symbolic);
					symbolicMap[(*currentEvent)->globalName] = value;
//					cerr << "load globalVarFullName : " << (*currentEvent)->globalVarFullName << "\n";
//					cerr << "load value : " << value << "\n";
					ref<Expr> constraint = EqExpr::create(value, symbolic);
//					cerr << "rwSymbolicExpr : " << constraint << "\n";
					trace->rwSymbolicExpr.push_back(constraint);
					trace->rwEvent.push_back(*currentEvent);
				}
			} else {
				//会丢失指针的一些关系约束，但是不影响。
				if (id == Type::PointerTyID && PTR) {
					ref<Expr> address = executor->eval(ki, 0, state).value;
					for (std::map<ref<Expr>, ref<Expr> >::iterator it = addressSymbolicMap.begin(), ie =
							addressSymbolicMap.end(); it != ie; ++it) {
						if (it->first == address){
//							cerr << "it->first : " << it->first << " it->second : " << it->second << "\n";
							executor->bindLocal(ki, state, it->second);
							break;
						}
					}
				} else {

				}
			}
			if (isFloat) {
				thread->stack->realStack.back().locals[ki->dest].value.get()->isFloat =
						true;
			}
			break;
		}

		case Instruction::Store: {
			break;
		}
		case Instruction::Call: {
			CallSite cs(inst);
			Function *f = (*currentEvent)->calledFunction;
			//可能存在未知错误
//			Value *fp = cs.getCalledValue();
//			Function *f = executor->getTargetFunction(fp, state);
//			if (!f) {
//				ref<Expr> expr = executor->eval(ki, 0, state).value;
//				ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr.get());
//				uint64_t functionPtr = constExpr->getZExtValue();
//				f = (Function*) functionPtr;
//			}

			//有待考证
//			if (!f->getName().startswith("klee") && !executor->kmodule->functionMap[f]) {
			if (!(*currentEvent)->isFunctionWithSourceCode) {
				ref<Expr> returnValue = executor->getDestCell(state, ki).value;
				bool isFloat = 0;
				Type::TypeID id = inst->getType()->getTypeID();
				if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
					isFloat = 1;
				}
				if (isFloat) {
					returnValue.get()->isFloat = true;
				}
				executor->bindLocal(ki, state, returnValue);
			}
//			if (!executor->kmodule->functionMap[f] && !inst->getType()->isVoidTy()) {
//				ref<Expr> value = executor->getDestCell(state, ki).value;
//				cerr << "value : " << value << "\n";
//			}

			//需要添加Map操作
			if (f->getName().startswith("klee_div_zero_check")) {
				kleeBr = true;
			} else if (f->getName().startswith("klee_overshift_check")) {
				kleeBr = true;
//			} else if (f->getName() == "strcpy") {
//				//地址可能还有问题
//				ref<Expr> destAddress = executor->eval(ki, 1, state).value;
////				ref<Expr> scrAddress = executor->eval(ki, 0,
////						stack[thread->threadId]).value;
////				ObjectPair scrop;
//				ObjectPair destop;
////				getMemoryObject(scrop, state, scrAddress);
//				executor->getMemoryObject(destop, state, destAddress);
//				const ObjectState* destos = destop.second;
//				const MemoryObject* destmo = destop.first;
////				std::cerr<<destAddress<<std::endl;
////				std::cerr<<destmo->address<<std::endl;
////				std::cerr<<"destmo->size : "<<destmo->size<<std::endl;
//				Expr::Width size = 8;
//				for (unsigned i = 0; i < (*currentEvent)->implicitGlobalVar.size(); i++) {
////					std::cerr<<"dest"<<std::endl;
//					ref<Expr> address = AddExpr::create(destAddress, ConstantExpr::create(i, BIT_WIDTH));
//					ref<Expr> value = destos->read(destmo->getOffsetExpr(address), size);
////					std::cerr<<"value : "<<value<<std::endl;
////					std::cerr<<"value : "<<value<<std::endl;
//					if (executor->isGlobalMO(destmo)) {
//						ref<Expr> value2 = manualMakeSymbolic(state,
//								(*currentEvent)->implicitGlobalVar[i], size, false);
//						ref<Expr> value1 = value;
//						ref<Expr> constraint = EqExpr::create(value1, value2);
//						trace->storeSymbolicExpr.push_back(constraint);
////						cerr << "constraint : " << constraint << "\n";
////						cerr << "Store Map varName : " << (*currentEvent)->varName << "\n";
////						cerr << "Store Map value : " << value << "\n";
//					}
//					if (value->isZero()) {
//						break;
//					}
//				}
			} else if (f->getName() == "pthread_create") {
				ref<Expr> pthreadAddress = executor->eval(ki, 1, state).value;
				ObjectPair pthreadop;
				executor->getMemoryObject(pthreadop, state, pthreadAddress);
				const ObjectState* pthreados = pthreadop.second;
				const MemoryObject* pthreadmo = pthreadop.first;
				Expr::Width size = BIT_WIDTH;
				ref<Expr> value = pthreados->read(0, size);
				if (executor->isGlobalMO(pthreadmo)) {
					string globalVarFullName = (*currentEvent)->globalName;
//					cerr << "globalVarFullName : " << globalVarFullName << "\n";
					symbolicMap[globalVarFullName] = value;
				}
//				cerr << "pthread id : " << value << "\n";
			}
			break;
		}
		case Instruction::PHI: {
//			ref<Expr> result = executor->eval(ki, thread->incomingBBIndex, state).value;
//			cerr << "PHI : " << result << "\n";
			break;
		}
		case Instruction::GetElementPtr: {
//			ref<Expr> value = executor->getDestCell(state, ki).value;
//			cerr << "value : " << value << "\n";
			break;
		}
		case Instruction::SExt: {
//			ref<Expr> value = executor->getDestCell(state, ki).value;
//			cerr << "value : " << value << "\n";
			break;
		}
		default: {

			break;
		}

		}
	}

	if (currentEvent != endEvent)
		currentEvent++;
}


//消息响应函数，在被测程序解释执行之后调用
void SymbolicListener::afterRunMethodAsMain() {
	symbolicMap.clear();
	addressSymbolicMap.clear();
	assertMap.clear();

	cerr << "######################本条路径为新路径####################\n";
#if EVENTS_DEBUG
	//true: output to file; false: output to terminal
	runtimeData.printCurrentTrace(true);
	//			encode.showInitTrace();//need to be modified
#endif
}

//消息相应函数，在前缀执行出错之后程序推出之前调用
void SymbolicListener::executionFailed(ExecutionState &state, KInstruction *ki) {
	runtimeData->getCurrentTrace()->traceType = Trace::FAILED;
}

ref<Expr> SymbolicListener::manualMakeSymbolic(ExecutionState& state,
		std::string name, unsigned size, bool isFloat) {

	//添加新的符号变量
	const Array *array = new Array(name, size);
	ObjectState *os = new ObjectState(size, array);
	ref<Expr> offset = ConstantExpr::create(0, BIT_WIDTH);
	ref<Expr> result = os->read(offset, size);
	if (isFloat) {
		result.get()->isFloat = true;
	}
#if DEBUGSYMBOLIC
	cerr << "Event name : " << (*currentEvent)->eventName << "\n";
	cerr << "make symboic:" << name << std::endl;
	cerr << "is float:" << isFloat << std::endl;
	std::cerr << "result : ";
	result->dump();
#endif
	return result;
}

ref<Expr> SymbolicListener::readExpr(ExecutionState &state, ref<Expr> address,
		Expr::Width size) {
	ObjectPair op;
	executor->getMemoryObject(op, state, address);
	const MemoryObject *mo = op.first;
	ref<Expr> offset = mo->getOffsetExpr(address);
	const ObjectState *os = op.second;
	ref<Expr> result = os->read(offset, size);
	return result;
}

void SymbolicListener::storeZeroToExpr(ExecutionState& state, ref<Expr> address,
		Expr::Width size) {

	ref<Expr> value = ConstantExpr::create(0, size);
	executor->executeMemoryOperation(state, true, address, value, 0);
}

}


