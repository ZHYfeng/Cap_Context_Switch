/*
 * TaintListener.cpp
 *
 *  Created on: 2016年2月17日
 *      Author: 11297
 */

#include "TaintListener.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/Casting.h>
#include <cassert>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../include/klee/Expr.h"
#include "../../include/klee/Internal/Module/Cell.h"
#include "../../include/klee/util/Ref.h"
#include "../Core/AddressSpace.h"
#include "Event.h"
#include "../Core/Memory.h"
#include "../Thread/StackType.h"
#include "../Core/Executor.h"
#include "Trace.h"

using namespace std;
using namespace llvm;

#define PTR 1
#define BIT_WIDTH 64
#define POINT_BIT_WIDTH 64

namespace klee {

	TaintListener::TaintListener(Executor* executor, RuntimeDataManager* rdManager) :
			BitcodeListener(rdManager), executor(executor), currentEvent(NULL) {
		kind = TaintListenerKind;
	}

	TaintListener::~TaintListener() {
		// TODO Auto-generated destructor stub

	}

//消息响应函数，在被测程序解释执行之前调用
	void TaintListener::beforeRunMethodAsMain(ExecutionState &initialState) {

	}

	void TaintListener::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		currentEvent = trace->path.back();

		Instruction* inst = ki->inst;
//		Thread* thread = state.currentThread;
//	llvm::errs() << "thread id : " << thread->threadId << " ";
//	inst->dump();
		if (currentEvent) {
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
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 1, state, value);
					}

					ref<Expr> value = executor->eval(ki, 0, state).value;
//					llvm::errs() << "value : ";
//					value->dump();
					bool isTaint = value->isTaint;
					std::vector<ref<klee::Expr> >* relatedSymbolicExpr = &(currentEvent->relatedSymbolicExpr);
					filter.resolveTaintExpr(value, currentEvent->relatedSymbolicExpr, isTaint);
//			llvm::errs() << "relatedSymbolicExpr" << "\n";
//			for (std::vector<ref<klee::Expr> >::iterator it = relatedSymbolicExpr->begin();
//					it != relatedSymbolicExpr->end(); it++) {
//				llvm::errs() << "name : " << *it << " isTaint : " << (*it)->isTaint << "\n";
//			}
					ObjectPair op;
					executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
					const MemoryObject *mo = op.first;
					const ObjectState *os = op.second;
					ObjectState *wos = state.currentStack->addressSpace->getWriteable(mo, os);
					if (isTaint) {
						wos->insertTaint(address);
					} else {
						wos->eraseTaint(address);
					}
					Type::TypeID id = ki->inst->getOperand(0)->getType()->getTypeID();
					bool isFloat = 0;
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (currentEvent->isGlobal) {
#if PTR
						if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
						if (isFloat || id == Type::IntegerTyID) {
#endif
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getOperand(0)->getType());
							ref<Expr> symbolic = manualMakeTaintSymbolic(state, currentEvent->globalName, size);

							//收集TS和PTS
							std::string varName = currentEvent->name;
							if (isTaint) {
								trace->DTAMSerial.insert(currentEvent->globalName);
								manualMakeTaint(symbolic, true);
								trace->taintSymbolicExpr.insert(varName);
								if (trace->unTaintSymbolicExpr.find(varName) != trace->unTaintSymbolicExpr.end()) {
									trace->unTaintSymbolicExpr.erase(varName);
								}
							} else {
								if (trace->taintSymbolicExpr.find(varName) == trace->taintSymbolicExpr.end()) {
									trace->unTaintSymbolicExpr.insert(varName);
								}
							}

							//编码tp
							ref<Expr> temp = ConstantExpr::create(0, size);
							for (std::vector<ref<klee::Expr> >::iterator it = relatedSymbolicExpr->begin(); it != relatedSymbolicExpr->end(); it++) {
								string varFullName = filter.getGlobalName(*it);
								ref<Expr> orExpr = manualMakeTaintSymbolic(state, varFullName, size);
								temp = OrExpr::create(temp, orExpr);
							}
							ref<Expr> constraint = EqExpr::create(temp, symbolic);
							trace->taintExpr.push_back(constraint);
//					llvm::errs() << constraint << "isTaint : " << isTaint << "\n" ;
						}
					}
					break;
				}

				case Instruction::Br: {
					BranchInst *bi = dyn_cast<BranchInst>(inst);
					if (!bi->isUnconditional()) {
						ref<Expr> value1 = executor->eval(ki, 0, state).value;
						if (value1->getKind() != Expr::Constant) {
							Expr::Width width = value1->getWidth();
							ref<Expr> value2;
							if (currentEvent->brCondition == true) {
								value2 = ConstantExpr::create(true, width);
							} else {
								value2 = ConstantExpr::create(false, width);
							}
							executor->ineval(ki, 0, state, value2);
						}
					}
					break;
				}
				case Instruction::Switch: {
					ref<Expr> cond1 = executor->eval(ki, 0, state).value;
					if (cond1->getKind() != Expr::Constant) {
						ref<Expr> cond2 = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, cond2);
					}
					break;
				}
				case Instruction::Call: {
					CallSite cs(inst);
					ref<Expr> function = executor->eval(ki, 0, state).value;
					if (function->getKind() == Expr::Concat) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						if (function->isTaint) {
							value->isTaint = true;
						}
						executor->ineval(ki, 0, state, value);
					}
					if (!currentEvent->isFunctionWithSourceCode) {
						unsigned numArgs = cs.arg_size();
						for (unsigned j = numArgs; j > 0; j--) {
							ref<Expr> value = executor->eval(ki, j, state).value;
							Type::TypeID id = cs.getArgument(j - 1)->getType()->getTypeID();
							bool isFloat = 0;
							if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
								isFloat = 1;
							}
							if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
								if (value->getKind() != Expr::Constant) {
									ref<Expr> svalue = executor->evalCurrent(ki, j, state).value;
									if (value->isTaint) {
										svalue->isTaint = true;
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
					if (base->getKind() != Expr::Constant) {
						ref<Expr> value = executor->evalCurrent(ki, 0, state).value;
						executor->ineval(ki, 0, state, value);
					}
					for (std::vector<std::pair<unsigned, uint64_t> >::iterator it = kgepi->indices.begin(), ie = kgepi->indices.end(); it != ie; ++it) {
						ref<Expr> index = executor->eval(ki, it->first, state).value;
						if (index->getKind() != Expr::Constant) {
							ref<Expr> value = executor->evalCurrent(ki, it->first, state).value;
							executor->ineval(ki, it->first, state, value);
							ref<Expr> constraint = EqExpr::create(index, value);
//					llvm::errs() << "event name : " << currentEvent->eventName << "\n";
//					llvm::errs() << "constraint : " << constraint << "\n";
						}
					}
					if (kgepi->offset) {

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

//TODO： Algorithm 2 AnalyseTaint
	void TaintListener::afterExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		if (currentEvent) {
			Instruction* inst = ki->inst;
			Thread* thread = state.currentThread;
			switch (inst->getOpcode()) {
				case Instruction::Load: {
					bool isFloat = 0;
					Type::TypeID id = ki->inst->getType()->getTypeID();
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (currentEvent->isGlobal) {
						for (unsigned i = 0; i < thread->vectorClock.size(); i++) {
							currentEvent->vectorClock.push_back(thread->vectorClock[i]);
//					llvm::errs() << "vectorClock " << i << " : " << currentEvent->vectorClock[i] << "\n";
						}
#if PTR
						if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
							if (isFloat || id == Type::IntegerTyID) {
#endif
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getType());
							ref<Expr> symbolic = manualMakeTaintSymbolic(state, currentEvent->globalName, size);
							executor->getDestCell(state, ki).value = symbolic;
//							llvm::errs() << "globalVarFullName : " << currentEvent->globalVarFullName << "\n";
						}
					}
					ref<Expr> address = executor->eval(ki, 0, state).value;
					ref<Expr> value = executor->getDestCell(state, ki).value;
					ObjectPair op;
					executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
					const ObjectState *os = op.second;
					bool isTaint = false;
					if (os->isTaint.find(address) != os->isTaint.end()) {
						isTaint = true;
					}
					if (isTaint) {
						manualMakeTaint(value, true);
						if (!inst->getType()->isPointerTy() && currentEvent->isGlobal) {
							trace->DTAMSerial.insert(currentEvent->globalName);
						}
					} else {
						manualMakeTaint(value, false);
					}
					executor->getDestCell(state, ki).value = value;
//					llvm::errs() << value << " taint : " << isTaint << "\n";
					break;
				}
				case Instruction::Store: {
					if (currentEvent->isGlobal) {
						for (unsigned i = 0; i < thread->vectorClock.size(); i++) {
							currentEvent->vectorClock.push_back(thread->vectorClock[i]);
//							llvm::errs() << "vectorClock " << i << " : " << currentEvent->vectorClock[i] << "\n";
						}
					}
					break;
				}
				case Instruction::Call: {
					CallSite cs(inst);
					Function *f = currentEvent->calledFunction;
					if (!currentEvent->isFunctionWithSourceCode && !inst->getType()->isVoidTy()) {
						unsigned numArgs = cs.arg_size();
						bool isTaint = 0;
						for (unsigned j = numArgs; j > 0; j--) {
							ref<Expr> value = executor->eval(ki, j, state).value;
							if (value->isTaint) {
								isTaint = true;
							}
						}
						if (isTaint) {
							executor->getDestCell(state, ki).value.get()->isTaint = true;
						}
					}

					if (f->getName() == "make_taint") {
						ref<Expr> address = executor->eval(ki, 1, state).value;
						ObjectPair op;
						executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
						const MemoryObject *mo = op.first;
						const ObjectState* os = op.second;
						ObjectState *wos = state.currentStack->addressSpace->getWriteable(mo, os);
						wos->insertTaint(address);

						trace->initTaintSymbolicExpr.insert(currentEvent->globalName);

					} else if (f->getName() == "pthread_create") {
						thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_join") {
						thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_cond_wait") {
						thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_cond_signal") {
						thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_cond_broadcast") {
						thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_mutex_lock") {
//				thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_mutex_unlock") {
//				thread->vectorClock[thread->threadId]++;
					} else if (f->getName().str() == "pthread_barrier_wait") {
						assert(0 && "目前没做");
					}
					break;
				}
				case Instruction::GetElementPtr: {
					break;
				}
				default: {

					break;
				}
			}
		}
		currentEvent++;
	}

//消息响应函数，在被测程序解释执行之后调用
	void TaintListener::afterRunMethodAsMain(ExecutionState &state) {

	}

//消息相应函数，在前缀执行出错之后程序推出之前调用
	void TaintListener::executionFailed(ExecutionState &state, KInstruction *ki) {

	}

	ref<Expr> TaintListener::manualMakeTaintSymbolic(ExecutionState& state, std::string name, unsigned size) {

		//添加新的污染符号变量
		//name maybe need add a "Tag"
		const Array *array = new Array(name, size);
		ObjectState *os = new ObjectState(size, array);
		ref<Expr> offset = ConstantExpr::create(0, BIT_WIDTH);
		ref<Expr> result = os->read(offset, size);
#if DEBUGSYMBOLIC
		llvm::errs() << "Event name : " << currentEvent->eventName << "\n";
		llvm::errs() << "make symboic:" << name << "\n";
		llvm::errs() << "isTaint:" << isTaint << "\n";
		llvm::errs() << "result : ";
		result->dump();
#endif
		return result;

	}

	void TaintListener::manualMakeTaint(ref<Expr> value, bool isTaint) {
		value->isTaint = isTaint;
	}

	ref<Expr> TaintListener::readExpr(ExecutionState &state, ref<Expr> address, Expr::Width size) {
		ObjectPair op;
		executor->getMemoryObject(op, state, state.currentStack->addressSpace, address);
		const MemoryObject *mo = op.first;
		ref<Expr> offset = mo->getOffsetExpr(address);
		const ObjectState *os = op.second;
		ref<Expr> result = os->read(offset, size);
		return result;
	}

}

