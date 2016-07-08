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

#define PTR 0
#define BIT_WIDTH 64
#define POINT_BIT_WIDTH 64

namespace klee {

	TaintListener::TaintListener(Executor* executor, RuntimeDataManager* rdManager) :
			BitcodeListener(rdManager), executor(executor) {
		kind = TaintListenerKind;
	}

	TaintListener::~TaintListener() {
		// TODO Auto-generated destructor stub

	}

//消息响应函数，在被测程序解释执行之前调用
	void TaintListener::beforeRunMethodAsMain(ExecutionState &initialState) {

		Trace* trace = rdManager->getCurrentTrace();
		currentEvent = trace->path.begin();
	}

	void TaintListener::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		Trace* trace = rdManager->getCurrentTrace();
		Instruction* inst = ki->inst;
//		Thread* thread = state.currentThread;
//	std::cerr << "thread id : " << thread->threadId << " ";
//	inst->dump();
		if ((*currentEvent)) {
			switch (inst->getOpcode()) {
				case Instruction::Br: {
					BranchInst *bi = dyn_cast<BranchInst>(inst);
					if (!bi->isUnconditional()) {
						ref<Expr> value1 = executor->eval(ki, 0, state).value;
						if (value1->getKind() != Expr::Constant) {
							Expr::Width width = value1->getWidth();
							ref<Expr> value2;
							if ((*currentEvent)->brCondition == true) {
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
//			SwitchInst *si = cast<SwitchInst>(inst);
					ref<Expr> cond1 = executor->eval(ki, 0, state).value;
					if (cond1->getKind() != Expr::Constant) {
						ref<Expr> cond2 = (*currentEvent)->instParameter.back();
						executor->ineval(ki, 0, state, cond2);
					}
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
						if (function->isTaint) {
							value->isTaint = true;
						}
						executor->ineval(ki, 0, state, value);
					}
					if (!(*currentEvent)->isFunctionWithSourceCode) {
						unsigned numArgs = cs.arg_size();
						for (unsigned j = numArgs; j > 0; j--) {
							ref<Expr> value = executor->eval(ki, j, state).value;
							Type::TypeID id = cs.getArgument(j - 1)->getType()->getTypeID();
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
										assert(0 && "pointer is expr::read");
									}
									if (value->isTaint) {
										svalue->isTaint = true;
									}
									executor->ineval(ki, j, state, svalue);
								} else {
									ref<Expr> svalue = (*currentEvent)->instParameter[j - 1];
									if (svalue->getKind() != Expr::Constant) {
										assert(0 && "store value is symbolic");
									} else if (id == Type::PointerTyID) {
										if (f->getName().str() == "pthread_create") {

										} else {
											assert(0 && "pointer is other symbolic");
										}
									}
									bool isTaint = value->isTaint;
									std::vector<ref<klee::Expr> > relatedSymbolicExpr;
									filter.resolveTaintExpr(value, relatedSymbolicExpr, isTaint);
									if (isTaint) {
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
//			cerr << "value : " << value << "\n";
					bool isTaint = value->isTaint;
					std::vector<ref<klee::Expr> >* relatedSymbolicExpr = &((*currentEvent)->relatedSymbolicExpr);
					filter.resolveTaintExpr(value, (*currentEvent)->relatedSymbolicExpr, isTaint);
//			cerr << "relatedSymbolicExpr" << "\n";
//			for (std::vector<ref<klee::Expr> >::iterator it = relatedSymbolicExpr->begin();
//					it != relatedSymbolicExpr->end(); it++) {
//				cerr << "name : " << *it << " isTaint : " << (*it)->isTaint << "\n";
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
					if ((*currentEvent)->isGlobal) {
#if PTR
						if (isFloat || id == Type::IntegerTyID
								|| id == Type::PointerTyID) {
#else
						if (isFloat || id == Type::IntegerTyID) {
#endif
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getOperand(0)->getType());
							ref<Expr> symbolic = manualMakeTaintSymbolic(state, (*currentEvent)->globalName, size);

							//收集TS和PTS
							std::string varName = (*currentEvent)->name;
							if (isTaint) {
								trace->DTAMSerial.insert((*currentEvent)->globalName);
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
//					cerr << constraint << "isTaint : " << isTaint << "\n" ;

							if (value->getKind() == Expr::Constant) {

							} else if (value->getKind() == Expr::Concat || value->getKind() == Expr::Read) {
								ref<Expr> svalue = symbolicMap[filter.getGlobalName(value)];
								if (svalue->getKind() != Expr::Constant) {
									assert(0 && "store value is symbolic");
								} else if (id == Type::PointerTyID && value->getKind() == Expr::Read) {
									assert(0 && "pointer is Expr::read");
								}
								if (value->isTaint) {
									svalue->isTaint = true;
								}
								executor->ineval(ki, 0, state, svalue);
							} else {
								ref<Expr> svalue = (*currentEvent)->instParameter.back();
								if (svalue->getKind() != Expr::Constant) {
									assert(0 && "store value is symbolic");
								} else if (id == Type::PointerTyID) {
									assert(0 && "pointer is other symbolic");
								}
								if (isTaint) {
									svalue->isTaint = true;
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
							if (value->getKind() == Expr::Concat) {
								ref<Expr> svalue = symbolicMap[filter.getGlobalName(value)];
								if (svalue->getKind() != Expr::Constant) {
									assert(0 && "store pointer is symbolic");
								}
								executor->ineval(ki, 0, state, svalue);
								ref<Expr> address = executor->eval(ki, 1, state).value;
								addressSymbolicMap[address] = value;
							} else if (value->getKind() == Expr::Read) {
								assert(0 && "pointer is expr::read");
							} else {
								ref<Expr> address = executor->eval(ki, 1, state).value;
								addressSymbolicMap[address] = value;
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
						assert(0 && "pointer is expr::read");
					}
					std::vector<ref<klee::Expr> >::iterator first = (*currentEvent)->instParameter.begin();
					for (std::vector<std::pair<unsigned, uint64_t> >::iterator it = kgepi->indices.begin(), ie = kgepi->indices.end(); it != ie; ++it) {
						ref<Expr> index = executor->eval(ki, it->first, state).value;
						if (index->getKind() != Expr::Constant) {
							executor->ineval(ki, it->first, state, *first);
						} else {
							if (index != *first) {
								assert(0 && "index != first");
							}
						}
						++first;
					}
					if (kgepi->offset) {

					}
					break;
				}
				case Instruction::PtrToInt: {
					ref<Expr> arg = executor->eval(ki, 0, state).value;
					if (arg->getKind() == Expr::Concat) {
						ref<Expr> value = symbolicMap[filter.getGlobalName(arg)];
						if (value->getKind() != Expr::Constant) {
							assert(0 && "GetElementPtr symbolic print");
						}
						executor->ineval(ki, 0, state, value);
					} else if (arg->getKind() == Expr::Read) {
						assert(0 && "pointer is expr::read");
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
		if ((*currentEvent)) {
			Instruction* inst = ki->inst;
			Thread* thread = state.currentThread;
			switch (inst->getOpcode()) {
				case Instruction::Load: {
					bool isFloat = 0;
					Type::TypeID id = ki->inst->getType()->getTypeID();
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if ((*currentEvent)->isGlobal) {

						for (unsigned i = 0; i < thread->vectorClock.size(); i++) {
							(*currentEvent)->vectorClock.push_back(thread->vectorClock[i]);
//					cerr << "vectorClock " << i << " : " << (*currentEvent)->vectorClock[i] << "\n";
						}

						//指针！！！
#if PTR
						if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
						if (isFloat || id == Type::IntegerTyID) {
#endif
							Expr::Width size = executor->getWidthForLLVMType(ki->inst->getType());
							ref<Expr> value = executor->getDestCell(state, ki).value;
							ref<Expr> symbolic = manualMakeTaintSymbolic(state, (*currentEvent)->globalName, size);
							executor->getDestCell(state, ki).value = symbolic;
							symbolicMap[(*currentEvent)->globalName] = value;
//					std::cerr << "globalVarFullName : " << (*currentEvent)->globalVarFullName << "\n";
						}
					} else {
						//会丢失指针的一些关系约束，但是不影响。
						if (id == Type::PointerTyID && PTR) {
							ref<Expr> address = executor->eval(ki, 0, state).value;
							for (std::map<ref<Expr>, ref<Expr> >::iterator it = addressSymbolicMap.begin(), ie = addressSymbolicMap.end(); it != ie; ++it) {
								if (it->first == address) {
									executor->getDestCell(state, ki).value = it->second;
									break;
								}
							}
						} else {

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
						if (!inst->getType()->isPointerTy() && (*currentEvent)->isGlobal) {
							trace->DTAMSerial.insert((*currentEvent)->globalName);

//					inst->dump();
						}

					} else {
						manualMakeTaint(value, false);
					}
					executor->getDestCell(state, ki).value = value;
//			cerr << value << " taint : " << isTaint << "\n";
					break;
				}
				case Instruction::Store: {
					if ((*currentEvent)->isGlobal) {
						for (unsigned i = 0; i < thread->vectorClock.size(); i++) {
							(*currentEvent)->vectorClock.push_back(thread->vectorClock[i]);
//					cerr << "vectorClock " << i << " : " << (*currentEvent)->vectorClock[i] << "\n";
						}
					}
					break;
				}
				case Instruction::Call: {
					CallSite cs(inst);
					Function *f = (*currentEvent)->calledFunction;
					if (!(*currentEvent)->isFunctionWithSourceCode) {
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

						trace->initTaintSymbolicExpr.insert((*currentEvent)->globalName);

					} else if (f->getName() == "pthread_create") {

						ref<Expr> pthreadAddress = executor->eval(ki, 1, state).value;
						ObjectPair pthreadop;
						executor->getMemoryObject(pthreadop, state, state.currentStack->addressSpace, pthreadAddress);
						const ObjectState* pthreados = pthreadop.second;
						const MemoryObject* pthreadmo = pthreadop.first;
						Expr::Width size = BIT_WIDTH;
						ref<Expr> value = pthreados->read(0, size);
						if (executor->isGlobalMO(pthreadmo)) {
							string globalVarFullName = (*currentEvent)->globalName;
							symbolicMap[globalVarFullName] = value;
						}

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
		cerr << "######################taint analysis####################\n";

	}

//消息相应函数，在前缀执行出错之后程序推出之前调用
	void TaintListener::executionFailed(ExecutionState &state, KInstruction *ki) {
		rdManager->getCurrentTrace()->traceType = Trace::FAILED;
	}

	ref<Expr> TaintListener::manualMakeTaintSymbolic(ExecutionState& state, std::string name, unsigned size) {

		//添加新的污染符号变量
		//name maybe need add a "Tag"
		const Array *array = new Array(name, size);
		ObjectState *os = new ObjectState(size, array);
		ref<Expr> offset = ConstantExpr::create(0, BIT_WIDTH);
		ref<Expr> result = os->read(offset, size);
#if DEBUGSYMBOLIC
		cerr << "Event name : " << (*currentEvent)->eventName << "\n";
		cerr << "make symboic:" << name << std::endl;
		cerr << "isTaint:" << isTaint << std::endl;
		std::cerr << "result : ";
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

