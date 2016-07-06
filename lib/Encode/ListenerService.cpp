/*
 * ListenerService.cpp
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#ifndef LIB_CORE_LISTENERSERVICE_C_
#define LIB_CORE_LISTENERSERVICE_C_

#include "ListenerService.h"

#include "../Core/Executor.h"
#include "DTAM.h"
#include "Encode.h"
#include "Prefix.h"
#include "PSOListener.h"
#include "SymbolicListener.h"
#include "TaintListener.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>

#include <stddef.h>
#include <sys/time.h>
#include <iterator>

namespace klee {

	ListenerService::ListenerService(Executor* executor) {
		encode = NULL;
		dtam = NULL;
		cost = 0;

	}

	ListenerService::~ListenerService() {

	}

	void ListenerService::pushListener(BitcodeListener* bitcodeListener) {
		bitcodeListeners.push_back(bitcodeListener);
	}

	void ListenerService::removeListener(BitcodeListener* bitcodeListener) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			if ((*bit) == bitcodeListener) {
				bitcodeListeners.erase(bit);
				break;
			}
		}
	}

	void ListenerService::removeListener(BitcodeListener::listenerKind kind) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			if ((*bit)->kind == kind) {
				bitcodeListeners.erase(bit);
				break;
			}
		}
	}

	void ListenerService::popListener() {
		bitcodeListeners.pop_back();
	}

	RuntimeDataManager* ListenerService::getRuntimeDataManager() {
		return &rdManager;
	}

	void ListenerService::beforeRunMethodAsMain(ExecutionState &initialState) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->beforeRunMethodAsMain(initialState);
		}
	}

	void ListenerService::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->beforeExecuteInstruction(state, ki);
		}
	}

	void ListenerService::afterExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->afterExecuteInstruction(state, ki);
		}
	}

	void ListenerService::afterRunMethodAsMain() {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->afterRunMethodAsMain();
		}
	}

	void ListenerService::executionFailed(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->executionFailed(state, ki);
		}
	}

	void ListenerService::executeInstruction(Executor* executor, ExecutionState &state, KInstruction *ki) {

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			state.currentStack = (*bit)->stack[state.currentThread->threadId];
			state.currentStack = state.currentThread->stack;
		}

		Instruction *i = ki->inst;
		switch (i->getOpcode()) {
			// Control flow
			case Instruction::Ret: {
				for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie;
						++bit) {
					state.currentStack = (*bit)->stack[state.currentThread->threadId];
					ReturnInst *ri = cast<ReturnInst>(i);
					KInstIterator kcaller = state.currentStack->realStack.back().caller;
					Instruction *caller = kcaller ? kcaller->inst : 0;
					bool isVoidReturn = (ri->getNumOperands() == 0);
					ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

					if (!isVoidReturn) {
						result = executor->eval(ki, 0, state).value;
					}

					state.currentStack->popFrame();

					if (!isVoidReturn) {
						LLVM_TYPE_Q Type *t = caller->getType();
						if (t != Type::getVoidTy(getGlobalContext())) {
							// may need to do coercion due to bitcasts
							Expr::Width from = result->getWidth();
							Expr::Width to = executor->getWidthForLLVMType(t);

							if (from != to) {
								CallSite cs = (
										isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) : CallSite(cast<CallInst>(caller)));

								bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
								if (isSExt) {
									result = SExtExpr::create(result, to);
								} else {
									result = ZExtExpr::create(result, to);
								}
							}

							executor->bindLocal(kcaller, state, result);
						}
					}
					state.currentStack = state.currentThread->stack;
				}
				break;
			}

			case Instruction::Invoke:
			case Instruction::Call: {
				CallSite cs(i);

				unsigned numArgs = cs.arg_size();
				Value *fp = cs.getCalledValue();
				Function *f = executor->getTargetFunction(fp, state);

				if (isa<InlineAsm>(fp)) {
					executor->terminateStateOnExecError(state, "inline assembly is unsupported");
					break;
				}
				// evaluate arguments
				std::vector<ref<Expr> > arguments;
				arguments.reserve(numArgs);

				for (unsigned j = 0; j < numArgs; ++j)
					arguments.push_back(executor->eval(ki, j + 1, state).value);

				if (f) {
					const FunctionType *fType = dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
					const FunctionType *fpType = dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());

					// special case the call with a bitcast case
					if (fType != fpType) {
						assert(fType && fpType && "unable to get function type");

						// XXX check result coercion

						// XXX this really needs thought and validation
						unsigned i = 0;
						for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), ie = arguments.end(); ai != ie; ++ai) {
							Expr::Width to, from = (*ai)->getWidth();

							if (i < fType->getNumParams()) {
								to = executor->getWidthForLLVMType(fType->getParamType(i));
								if (from != to) {
									bool isSExt = cs.paramHasAttr(i + 1, llvm::Attribute::SExt);
									if (isSExt) {
										arguments[i] = SExtExpr::create(arguments[i], to);
									} else {
										arguments[i] = ZExtExpr::create(arguments[i], to);
									}
								}
							}

							i++;
						}
					}

					executor->executeCall(state, ki, f, arguments);
				} else {
					assert(0 && "listenerSercive execute call");
				}
				break;
			}

				// Memory instructions...
			case Instruction::Alloca: {
				AllocaInst *ai = cast<AllocaInst>(i);
				unsigned elementSize = executor->kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
				ref<Expr> size = Expr::createPointer(elementSize);
				if (ai->isArrayAllocation()) {
					ref<Expr> count = executor->eval(ki, 0, state).value;
					count = Expr::createZExtToPointerWidth(count);
					size = MulExpr::create(size, count);
				}
				bool isLocal = true;
				size = executor->toUnique(state, size);
				if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
					ref<Expr> addr = executor->getDestCell(state, ki).value;
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, addr);
					if (success) {
						const MemoryObject *mo = op.first;
						for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end();
								bit != bie; ++bit) {
							state.currentStack = (*bit)->stack[state.currentThread->threadId];
							ObjectState *os = executor->bindObjectInState(state, mo, isLocal);
							os->initializeToRandom();
							executor->bindLocal(ki, state, mo->getBaseExpr());
							state.currentStack = state.currentThread->stack;
						}
					} else {
						for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end();
								bit != bie; ++bit) {
							state.currentStack = (*bit)->stack[state.currentThread->threadId];
							executor->bindLocal(ki, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
							state.currentStack = state.currentThread->stack;
						}
						executor->bindLocal(ki, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
					}
				}
				break;
			}

			case Instruction::Br:
			case Instruction::Switch: {
				break;
			}

			default: {
				for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie;
						++bit) {
					state.currentStack = (*bit)->stack[state.currentThread->threadId];
					executor->executeInstruction(state, ki);
					state.currentStack = state.currentThread->stack;
				}
				break;
			}

		}

	}

	void ListenerService::startControl(Executor* executor) {

		executor->executionNum++;

		BitcodeListener* PSOlistener = new PSOListener(executor, &rdManager);
		pushListener(PSOlistener);
		BitcodeListener* Symboliclistener = new SymbolicListener(executor, &rdManager);
		pushListener(Symboliclistener);
		BitcodeListener* Taintlistener = new TaintListener(executor, &rdManager);
		pushListener(Taintlistener);

		gettimeofday(&start, NULL);

	}

	void ListenerService::endControl(Executor* executor) {

		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.runningCost += cost;

		gettimeofday(&start, NULL);
		encode = new Encode(&rdManager);
		encode->buildifAndassert();
		if (encode->verify()) {
			encode->check_if();
		}
		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.solvingCost += cost;

		gettimeofday(&start, NULL);
		dtam = new DTAM(&rdManager);
		dtam->dtam();
		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.DTAMCost += cost;
		rdManager.allDTAMCost.push_back(cost);

		gettimeofday(&start, NULL);
		encode->PTS();
		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.PTSCost += cost;
		rdManager.allPTSCost.push_back(cost);

		executor->getNewPrefix();

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			delete *bit;
		}

		delete encode;
		delete dtam;

	}

}

#endif
