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

namespace klee {

	SymbolicListener::SymbolicListener(Executor* executor, RuntimeDataManager* rdManager) :
			BitcodeListener(rdManager), executor(executor), currentEvent(NULL) {
		kind = SymbolicListenerKind;
		kleeBr = false;
	}

	SymbolicListener::~SymbolicListener() {

	}

//消息响应函数，在被测程序解释执行之前调用
	void SymbolicListener::beforeRunMethodAsMain(ExecutionState &initialState) {

		//收集assert
		for (std::vector<KFunction*>::iterator i = executor->kmodule->functions.begin(), e = executor->kmodule->functions.end(); i != e;
				++i) {
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
//						printf("fileName : %s, line : %d\n",fileName.c_str(),line);
//						llvm::errs() << "call name : " << f->getName().str() << "\n";
					}
				}
			}
		}
	}

	void SymbolicListener::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		currentEvent = trace->path.back();

		if (currentEvent) {
			Instruction* inst = ki->inst;
//		Thread* thread = state.currentThread;
//		llvm::errs() << "event name : " << currentEvent->eventName << " ";
//		llvm::errs() << "thread
//		llvm::errs() << "thread id : " << currentEvent->threadId ;
//		currentEvent->inst->inst->dump();
			switch (inst->getOpcode()) {
				case Instruction::Load: {
					ref<Expr> address = executor->eval(ki, 0, state).value;
					if (address->getKind() == Expr::Concat) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, value);
					}
					break;
				}
				case Instruction::Store: {
					ref<Expr> address = executor->eval(ki, 1, state).value;
					if (address->getKind() == Expr::Concat) {
						ref<Expr> value = executor->evalCurrent(ki, 1, state).value;
						executor->ineval(ki, 1, state, value);
					}

					ref<Expr> value = executor->eval(ki, 0, state).value;
					Type::TypeID id = ki->inst->getOperand(0)->getType()->getTypeID();
//			llvm::errs() << "store value : " << value << "\n";
//			llvm::errs() << "store base : " << base << "\n";
//			llvm::errs() << "value->getKind() : " << value->getKind() << "\n";
//			llvm::errs() << "TypeID id : " << id << "\n";
					bool isFloat = 0;
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (currentEvent->isGlobal) {
						if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getOperand(0)->getType());
//							ref<Expr> address = executor->eval(ki, 1, state).value;
							ref<Expr> symbolic = manualMakeSymbolic(state, currentEvent->globalName, size, isFloat);
							ref<Expr> constraint = EqExpr::create(value, symbolic);
							trace->storeSymbolicExpr.push_back(constraint);
//					llvm::errs() << "event name : " << currentEvent->eventName << "\n";
//					llvm::errs() << "store constraint : " << constraint << "\n";
						}
					}
					break;
				}
				case Instruction::Br: {
					BranchInst *bi = dyn_cast<BranchInst>(inst);
					if (!bi->isUnconditional()) {
						unsigned isAssert = 0;
						string fileName = ki->info->file;
						std::map<string, std::vector<unsigned> >::iterator it = assertMap.find(fileName);
						unsigned line = ki->info->line;
						if (it != assertMap.end()) {
							if (find(assertMap[fileName].begin(), assertMap[fileName].end(), line) != assertMap[fileName].end()) {
								isAssert = 1;
							}
						}
						ref<Expr> value1 = executor->eval(ki, 0, state).value;
						if (value1->getKind() != Expr::Constant) {
							Expr::Width width = value1->getWidth();
							ref<Expr> value2;
							if (currentEvent->brCondition == true) {
								value2 = ConstantExpr::create(true, width);
							} else {
								value2 = ConstantExpr::create(false, width);
							}
							ref<Expr> constraint = EqExpr::create(value1, value2);
							if (isAssert) {
#if DEBUGSYMBOLIC
								errs() << "event name : " << currentEvent->eventName << "\n";
								errs() << "assert constraint : " << constraint << "\n";
#endif
								trace->assertSymbolicExpr.push_back(constraint);
								trace->assertEvent.push_back(currentEvent);
							} else if (kleeBr == false) {
#if DEBUGSYMBOLIC
								errs() << "event name : " << currentEvent->eventName << "\n";
								errs() << "br constraint : " << constraint << "\n";
#endif
								trace->brSymbolicExpr.push_back(constraint);
								trace->brEvent.push_back(currentEvent);
							}
							executor->ineval(ki, 0, state, value2);
						}
						if (kleeBr == true) {
							kleeBr = false;
						}
					}
					break;
				}
				case Instruction::Switch: {
					ref<Expr> cond1 = executor->eval(ki, 0, state).value;
					if (cond1->getKind() != Expr::Constant) {
						ref<Expr> cond2 = executor->evalCurrent(ki, 0, state).value;
						ref<Expr> constraint = EqExpr::create(cond1, cond2);
						trace->brSymbolicExpr.push_back(constraint);
						trace->brEvent.push_back(currentEvent);
						executor->ineval(ki, 0, state, cond2);
					}
					break;
				}
				case Instruction::Call: {
					CallSite cs(inst);
					ref<Expr> function = executor->eval(ki, 0, state).value;
					if (function->getKind() != Expr::Constant) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, value);
					}
//			llvm::errs()<<"isFunctionWithSourceCode : ";
//					currentEvent->inst->inst->dump();
//			llvm::errs()<<"isFunctionWithSourceCode : ";
//					inst->dump();
//			llvm::errs()<<"isFunctionWithSourceCode : "<<currentEvent->isFunctionWithSourceCode<<"\n";
					if (!currentEvent->isFunctionWithSourceCode) {
						unsigned numArgs = cs.arg_size();
						for (unsigned j = numArgs; j > 0; j--) {
							ref<Expr> value = executor->eval(ki, j, state).value;
							Type::TypeID id = cs.getArgument(j - 1)->getType()->getTypeID();
//					llvm::errs() << "value->getKind() : " << value->getKind() << "\n";
//					llvm::errs() << "TypeID id : " << id << "\n";
//		    		llvm::errs()<<"value : " << value << "\n";
							bool isFloat = 0;
							if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
								isFloat = 1;
							}
							if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
								if (value->getKind() != Expr::Constant) {
									ref<Expr> svalue = executor->evalCurrent(ki, j, state).value;
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
					if (base->getKind() != Expr::Constant) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, value);
					}
//					llvm::errs() << "kgepi->base : " << base << "\n";
					for (std::vector<std::pair<unsigned, uint64_t> >::iterator it = kgepi->indices.begin(), ie = kgepi->indices.end();
							it != ie; ++it) {
						ref<Expr> index = executor->eval(ki, it->first, state).value;
//					llvm::errs() << "kgepi->index : " << index << "\n";
//					llvm::errs() << "first : " << *first << "\n";
						if (index->getKind() != Expr::Constant) {
							ref<Expr> value = executor->evalCurrent(ki, it->first, state).value;
							executor->ineval(ki, it->first, state, value);
							ref<Expr> constraint = EqExpr::create(index, value);
//					llvm::errs() << "event name : " << currentEvent->eventName << "\n";
//					llvm::errs() << "constraint : " << constraint << "\n";
							trace->brSymbolicExpr.push_back(constraint);
							trace->brEvent.push_back(currentEvent);
						}
					}
					if (kgepi->offset) {
//						llvm::errs() << "kgepi->offset : " << kgepi->offset << "\n";
						//目前没有这种情况...
//						assert(0 && "kgepi->offset");
					}
					break;
				}
				case Instruction::PtrToInt: {
					ref<Expr> arg = executor->eval(ki, 0, state).value;
					if (arg->getKind() != Expr::Constant) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, value);
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
		Trace* trace = rdManager->getCurrentTrace();
		if (currentEvent) {
			Instruction* inst = ki->inst;
			Thread* thread = state.currentThread;
			switch (inst->getOpcode()) {
				case Instruction::Load: {
//			llvm::errs() << "value : " << value << "\n";
					bool isFloat = 0;
					Type::TypeID id = ki->inst->getType()->getTypeID();
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (currentEvent->isGlobal) {

						//指针！！！
#if PTR
						if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
							if (isFloat || id == Type::IntegerTyID) {
#endif
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getType());
							ref<Expr> value = executor->getDestCell(state, ki).value;
							ref<Expr> symbolic = manualMakeSymbolic(state, currentEvent->globalName, size, isFloat);
							executor->bindLocal(ki, state, symbolic);
//							llvm::errs() << "load globalVarFullName : " << currentEvent->globalVarFullName << "\n";
//							llvm::errs() << "load value : " << value << "\n";
							ref<Expr> constraint = EqExpr::create(value, symbolic);
//							llvm::errs() << "rwSymbolicExpr : " << constraint << "\n";
							trace->rwSymbolicExpr.push_back(constraint);
							trace->rwEvent.push_back(currentEvent);
						}
					}
					if (isFloat) {
						thread->stack->realStack.back().locals[ki->dest].value.get()->isFloat = true;
					}
					break;
				}

				case Instruction::Store: {
					break;
				}
				case Instruction::Call: {
					CallSite cs(inst);
					Function *f = currentEvent->calledFunction;
					//可能存在未知错误
//					Value *fp = cs.getCalledValue();
//					Function *f = executor->getTargetFunction(fp, state);
//					if (!f) {
//						ref<Expr> expr = executor->eval(ki, 0, state).value;
//						ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr.get());
//						uint64_t functionPtr = constExpr->getZExtValue();
//						f = (Function*) functionPtr;
//					}

					if (!currentEvent->isFunctionWithSourceCode && !inst->getType()->isVoidTy()) {
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
//				llvm::errs() << "value : " << value << "\n";
//			}
					if (f->getName().startswith("klee_div_zero_check")) {
						kleeBr = true;
					} else if (f->getName().startswith("klee_overshift_check")) {
						kleeBr = true;
					}
					break;
				}
				case Instruction::PHI: {
//			ref<Expr> result = executor->eval(ki, thread->incomingBBIndex, state).value;
//			llvm::errs() << "PHI : " << result << "\n";
					break;
				}
				case Instruction::GetElementPtr: {
//			ref<Expr> value = executor->getDestCell(state, ki).value;
//			llvm::errs() << "value : " << value << "\n";
					break;
				}
				case Instruction::SExt: {
//			ref<Expr> value = executor->getDestCell(state, ki).value;
//			llvm::errs() << "value : " << value << "\n";
					break;
				}
				default: {

					break;
				}

			}
		}
	}

//消息响应函数，在被测程序解释执行之后调用
	void SymbolicListener::afterRunMethodAsMain(ExecutionState &state) {
		assertMap.clear();
	}

//消息相应函数，在前缀执行出错之后程序推出之前调用
	void SymbolicListener::executionFailed(ExecutionState &state, KInstruction *ki) {

	}

	ref<Expr> SymbolicListener::manualMakeSymbolic(ExecutionState& state, std::string name, unsigned size, bool isFloat) {

		//添加新的符号变量
		const Array *array = new Array(name, size);
		ObjectState *os = new ObjectState(size, array);
		ref<Expr> offset = ConstantExpr::create(0, BIT_WIDTH);
		ref<Expr> result = os->read(offset, size);
		if (isFloat) {
			result.get()->isFloat = true;
		}
#if DEBUGSYMBOLIC
		llvm::errs() << "event name : " << currentEvent->eventName << "\n";
		llvm::errs() << "make symboic:" << name << "\n";
		llvm::errs() << "is float:" << isFloat << "\n";
		llvm::errs() << "result : ";
		result->dump();
#endif
		return result;
	}

}

