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
#include "../Core/ExternalDispatcher.h"
#include "../Thread/StackType.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Intrinsics.h>

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

	void ListenerService::beforeRunMethodAsMain(Executor* executor, ExecutionState &state, llvm::Function *f, MemoryObject *argvMO,
			std::vector<ref<Expr> > arguments, int argc, char **argv, char **envp) {

		Module* m = executor->kmodule->module;
		unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
		int envc;
		for (envc = 0; envp[envc]; ++envc)
			;

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			state.currentStack = (*bit)->stack[state.currentThread->threadId];
			state.currentStack->pushFrame(0, executor->kmodule->functionMap[f]);

			for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
				executor->bindArgument(executor->kmodule->functionMap[f], i, state, arguments[i]);
			if (argvMO) {
				const ObjectState *argvOSCurrent = state.currentThread->addressSpace->findObject(argvMO);
				ObjectState *argvOS = executor->bindObjectInState(state, argvMO, false);
				for (int i = 0; i < argc + 1 + envc + 1 + 1; i++) {
					if (i == argc || i >= argc + 1 + envc) {
						// Write NULL pointer
						argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
					} else {
						char *s = i < argc ? argv[i] : envp[i - (argc + 1)];
						int j, len = strlen(s);
						ref<Expr> argAddr = argvOSCurrent->read(i * NumPtrBytes, Context::get().getPointerWidth());
						ObjectPair op;
						bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, argAddr);
						if (success) {
							const MemoryObject *arg = op.first;
							ObjectState *os = executor->bindObjectInState(state, arg, false);
							for (j = 0; j < len + 1; j++) {
								os->write8(j, s[j]);
							}
							argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
						}
					}
				}
			}

//			llvm::errs() << (*bit)->kind << "\n";
			for (llvm::Module::const_global_iterator i = m->global_begin(), e = m->global_end(); i != e; ++i) {
				if (i->isDeclaration()) {
//					llvm::errs() << "i->isDeclaration()\n";
					LLVM_TYPE_Q Type *ty = i->getType()->getElementType();
					uint64_t size = executor->kmodule->targetData->getTypeStoreSize(ty);
					MemoryObject *mo = executor->globalObjects.find(i)->second;
					ObjectState *os = executor->bindObjectInState(state, mo, false);
					if (size) {
						void *addr;
						if (i->getName() == "__dso_handle") {
							addr = &__dso_handle; // wtf ?
						} else {
							addr = executor->externalDispatcher->resolveSymbol(i->getName());
						}
						for (unsigned offset = 0; offset < mo->size; offset++)
							os->write8(offset, ((unsigned char*) addr)[offset]);
					}
				} else {
//					llvm::errs() << "!i->isDeclaration()\n";
					MemoryObject *mo = executor->globalObjects.find(i)->second;
//					llvm::errs() << "MemoryObject *mo = executor->globalObjects.find(i)->second;\n";
					ObjectState *os = executor->bindObjectInState(state, mo, false);
//					llvm::errs() << "ObjectState *os = executor->bindObjectInState(state, mo, false);\n";
					if (!i->hasInitializer())
						os->initializeToRandom();
				}
			}
			for (llvm::Module::const_global_iterator i = m->global_begin(), e = m->global_end(); i != e; ++i) {
				if (i->hasInitializer()) {
					MemoryObject *mo = executor->globalObjects.find(i)->second;
					const ObjectState *os = state.currentStack->addressSpace->findObject(mo);
					ObjectState *wos = state.currentStack->addressSpace->getWriteable(mo, os);
					executor->initializeGlobalObject(state, wos, i->getInitializer(), 0);
				}
			}

			(*bit)->beforeRunMethodAsMain(state);

			state.currentStack = state.currentThread->stack;
		}
	}

	void ListenerService::beforeExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki) {

#if !DEBUG_RUNTIME
		llvm::errs() << "thread id : " << state.currentThread->threadId << "  ";
		ki->inst->dump();
//		llvm::errs() << " before : " << "prefix : " << executor->prefix->isFinished() << "\n";
#endif

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			state.currentStack = (*bit)->stack[state.currentThread->threadId];

			(*bit)->beforeExecuteInstruction(state, ki);

			state.currentStack = state.currentThread->stack;
		}

		Instruction *i = ki->inst;

		switch (i->getOpcode()) {
			case Instruction::Ret: {
				break;
			}

			case Instruction::Invoke:
			case Instruction::Call: {
				for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie;
						++bit) {
					state.currentStack = (*bit)->stack[state.currentThread->threadId];

					CallSite cs(i);
					unsigned numArgs = cs.arg_size();
					Value *fp = cs.getCalledValue();
					Function *f = executor->getTargetFunction(fp, state);
					(*bit)->arguments.reserve(numArgs);
					for (unsigned j = 0; j < numArgs; ++j) {
						(*bit)->arguments.push_back(executor->eval(ki, j + 1, state).value);
					}
//					llvm::errs() << "arguments.reserve(numArgs);\n";
					if (f) {
						const FunctionType *fType = dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
						const FunctionType *fpType = dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());
						if (fType != fpType) {
							unsigned i = 0;
							for (std::vector<ref<Expr> >::iterator ai = (*bit)->arguments.begin(), ie = (*bit)->arguments.end(); ai != ie;
									++ai) {
								Expr::Width to, from = (*ai)->getWidth();
								if (i < fType->getNumParams()) {
									to = executor->getWidthForLLVMType(fType->getParamType(i));
									if (from != to) {
										bool isSExt = cs.paramHasAttr(i + 1, llvm::Attribute::SExt);
										if (isSExt) {
											(*bit)->arguments[i] = SExtExpr::create((*bit)->arguments[i], to);
										} else {
											(*bit)->arguments[i] = ZExtExpr::create((*bit)->arguments[i], to);
										}
									}
								}
								i++;
							}
						}
						if (f && f->isDeclaration()) {
							switch (f->getIntrinsicID()) {
								case Intrinsic::not_intrinsic: {
									break;
								}
								case Intrinsic::vastart: {
									StackFrame &sf = state.currentStack->realStack.back();
									Expr::Width WordSize = Context::get().getPointerWidth();
									if (WordSize == Expr::Int32) {
										executor->executeMemoryOperation(state, true, (*bit)->arguments[0], sf.varargs->getBaseExpr(), 0);
									} else {
										executor->executeMemoryOperation(state, true, (*bit)->arguments[0], ConstantExpr::create(48, 32),
												0); // gp_offset
										executor->executeMemoryOperation(state, true,
												AddExpr::create((*bit)->arguments[0], ConstantExpr::create(4, 64)),
												ConstantExpr::create(304, 32), 0); // fp_offset
										executor->executeMemoryOperation(state, true,
												AddExpr::create((*bit)->arguments[0], ConstantExpr::create(8, 64)),
												sf.varargs->getBaseExpr(), 0); // overflow_arg_area
										executor->executeMemoryOperation(state, true,
												AddExpr::create((*bit)->arguments[0], ConstantExpr::create(16, 64)),
												ConstantExpr::create(0, 64), 0); // reg_save_area
									}
									break;
								}
								default:
									break;
							}
						} else {

						}
					} else {
						assert(0 && "listenerSercive execute call");
					}
					state.currentStack = state.currentThread->stack;
				}
				break;
			}

			case Instruction::Alloca: {
				break;
			}
			case Instruction::Br: {
				break;
			}
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

	void ListenerService::afterExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki) {

//		llvm::errs() << " after : " << "\n";

		Instruction *i = ki->inst;

		switch (i->getOpcode()) {

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
					if (state.currentStack->realStack.size() <= 1) {

					} else {
//						llvm::errs() << " state.currentStack->popFrame();state.currentStack->popFrame(); : " << "\n";
						state.currentStack->popFrame();
//						llvm::errs() << " state.currentStack->popFrame(); : " << "\n";
						if (!isVoidReturn) {
							LLVM_TYPE_Q Type *t = caller->getType();
							if (t != Type::getVoidTy(getGlobalContext())) {
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
				}
				break;
			}

			case Instruction::Invoke:
			case Instruction::Call: {
				for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie;
						++bit) {
					state.currentStack = (*bit)->stack[state.currentThread->threadId];
					CallSite cs(i);
					Value *fp = cs.getCalledValue();
					Function *f = executor->getTargetFunction(fp, state);
					if (f) {
						if (f && f->isDeclaration()) {
							switch (f->getIntrinsicID()) {
								case Intrinsic::not_intrinsic:

									if (f->getName().str() == "pthread_create") {
										CallInst* calli = dyn_cast<CallInst>(ki->inst);
										Value* threadEntranceFP = calli->getArgOperand(2);
										Function *threadEntrance = executor->getTargetFunction(threadEntranceFP, state);
										if (!threadEntrance) {
											ref<Expr> param = executor->eval(ki, 3, state).value;
											ConstantExpr* functionPtr = dyn_cast<ConstantExpr>(param);
											threadEntrance = (Function*) (functionPtr->getZExtValue());
										}
										KFunction *kthreadEntrance = executor->kmodule->functionMap[threadEntrance];
										PointerType* pointerType = (PointerType*) (calli->getArgOperand(0)->getType());
										IntegerType* elementType = (IntegerType*) (pointerType->getElementType());
										Expr::Width type = elementType->getBitWidth();
										ref<Expr> address = (*bit)->arguments[0];
										ObjectPair op;
										bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, address);
										if (success) {
											const MemoryObject *mo = op.first;
											ref<Expr> offset = mo->getOffsetExpr(address);
											const ObjectState *os = op.second;
											ref<Expr> threadID = os->read(offset, type);
//											llvm::errs() << "thread id : " << threadID << "\n";
											executor->executeMemoryOperation(state, true, address, threadID, 0);
											executor->bindLocal(ki, state, ConstantExpr::create(0, Expr::Int32));

											StackType *stack = new StackType(&((*bit)->addressSpace));
											(*bit)->stack[dyn_cast<ConstantExpr>(threadID)->getAPValue().getSExtValue()] = stack;
											stack->realStack.reserve(10);
											stack->pushFrame(0, kthreadEntrance);
											state.currentStack = stack;
											executor->bindArgument(kthreadEntrance, 0, state, (*bit)->arguments[3]);
#if DEBUG_RUNTIME
											llvm::errs() << "(*bit)->arguments[3] : " << (*bit)->arguments[3] << "\n";
#endif
											state.currentStack = (*bit)->stack[state.currentThread->threadId];
										}

									} else if (f->getName().str() == "malloc" || f->getName().str() == "_ZdaPv"
											|| f->getName().str() == "_Znaj" || f->getName().str() == "_Znam"
											|| f->getName().str() == "valloc") {
										ref<Expr> size = (*bit)->arguments[0];
										bool isLocal = false;
										size = executor->toUnique(state, size);
										if (dyn_cast<ConstantExpr>(size)) {
											ref<Expr> addr = state.currentThread->stack->realStack.back().locals[ki->dest].value;
											ObjectPair op;
											bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, addr);
											if (success) {
												const MemoryObject *mo = op.first;
#if DEBUG_RUNTIME
												llvm::errs() << "mo address : " << mo->address << " mo size : " << mo->size << "\n";
#endif
												ObjectState *os = executor->bindObjectInState(state, mo, isLocal);
												os->initializeToRandom();
												executor->bindLocal(ki, state, mo->getBaseExpr());
											} else {
												executor->bindLocal(ki, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
											}
										}
									} else if (f->getName().str() == "_ZdlPv" || f->getName().str() == "_Znwj"
											|| f->getName().str() == "_Znwm" || f->getName().str() == "free") {
										ref<Expr> address = (*bit)->arguments[0];
//										llvm::errs() << "address : " << address << "\n";
										Executor::StatePair zeroPointer = executor->fork(state, Expr::createIsZero(address), true);
										if (zeroPointer.first) {
											if (ki)
												executor->bindLocal(ki, *zeroPointer.first, Expr::createPointer(0));
										}
										if (zeroPointer.second) { // address != 0
											Executor::ExactResolutionList rl;
											executor->resolveExact(*zeroPointer.second, address, rl, "free");
											for (Executor::ExactResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
												const MemoryObject *mo = it->first.first;
												if (mo->isLocal) {
													executor->terminateStateOnError(*it->second, "free of alloca", "free.err",
															executor->getAddressInfo(*it->second, address));
												} else if (mo->isGlobal) {
													executor->terminateStateOnError(*it->second, "free of global", "free.err",
															executor->getAddressInfo(*it->second, address));
												} else {
													it->second->currentStack->addressSpace->unbindObject(mo);
													if (ki)
														executor->bindLocal(ki, *it->second, Expr::createPointer(0));
												}
											}
										}
									} else if (f->getName().str() == "calloc") {
										ref<Expr> size = MulExpr::create((*bit)->arguments[0], (*bit)->arguments[1]);
										bool isLocal = false;
										size = executor->toUnique(state, size);
										if (dyn_cast<ConstantExpr>(size)) {
											ref<Expr> addr = state.currentThread->stack->realStack.back().locals[ki->dest].value;
//											llvm::errs() << "calloc address : ";
//											addr->dump();
											ObjectPair op;
											bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, addr);
											if (success) {
												const MemoryObject *mo = op.first;
//												llvm::errs() << "calloc address ; " << mo->address << " calloc size : " << mo->size << "\n";
												ObjectState *os = executor->bindObjectInState(state, mo, isLocal);
												os->initializeToRandom();
												executor->bindLocal(ki, state, mo->getBaseExpr());
											} else {
												executor->bindLocal(ki, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
											}
										}
									} else if (f->getName().str() == "realloc") {
										assert(0 && "realloc");
									} else {
//										(*bit)->addressSpace.copyInConcretes();
										LLVM_TYPE_Q Type *resultType = ki->inst->getType();
										if (resultType != Type::getVoidTy(getGlobalContext())) {
											ref<Expr> e = state.currentThread->stack->realStack.back().locals[ki->dest].value;
											executor->bindLocal(ki, state, e);
										}
									}
									break;
								case Intrinsic::vastart: {
									break;
								}
								default:
									break;
							}
						} else {
							KFunction *kf = executor->kmodule->functionMap[f];
							state.currentStack->pushFrame(state.currentThread->prevPC, kf);
							unsigned callingArgs = (*bit)->arguments.size();
							unsigned funcArgs = f->arg_size();
							if (f->isVarArg()) {
								Expr::Width WordSize = Context::get().getPointerWidth();
								StackFrame &sf = state.currentStack->realStack.back();
								MemoryObject *mo = sf.varargs = state.currentThread->stack->realStack.back().varargs;
								ObjectState *os = executor->bindObjectInState(state, mo, true);
								unsigned offset = 0;
								for (unsigned i = funcArgs; i < callingArgs; i++) {
									if (WordSize == Expr::Int32) {
										os->write(offset, (*bit)->arguments[i]);
										offset += Expr::getMinBytesForWidth((*bit)->arguments[i]->getWidth());
									} else {
										Expr::Width argWidth = (*bit)->arguments[i]->getWidth();
										if (argWidth > Expr::Int64) {
											offset = llvm::RoundUpToAlignment(offset, 16);
										}
										os->write(offset, (*bit)->arguments[i]);
										offset += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
									}
								}
							}
							unsigned numFormals = f->arg_size();
							for (unsigned i = 0; i < numFormals; ++i) {
								executor->bindArgument(kf, i, state, (*bit)->arguments[i]);
							}
						}
					} else {
						assert(0 && "listenerSercive execute call");
					}
					(*bit)->arguments.clear();
					state.currentStack = state.currentThread->stack;
				}
				break;
			}

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
				if (dyn_cast<ConstantExpr>(size)) {
					ref<Expr> addr = executor->getDestCell(state, ki).value;
//					llvm::errs() << "alloc address : ";
//					addr->dump();
					ObjectPair op;
					bool success = executor->getMemoryObject(op, state, state.currentThread->addressSpace, addr);
					if (success) {
						const MemoryObject *mo = op.first;
//						llvm::errs() << "alloc address ; " << mo->address << " alloc size : " << mo->size << "\n";
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
					}
				}
				break;
			}

			case Instruction::Br: {
				break;
			}
			case Instruction::Switch: {
				break;
			}

			default: {
				break;
			}

		}

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			state.currentStack = (*bit)->stack[state.currentThread->threadId];

			(*bit)->afterExecuteInstruction(state, ki);

			state.currentStack = state.currentThread->stack;
		}
	}

	void ListenerService::afterRunMethodAsMain(ExecutionState &state) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->afterRunMethodAsMain(state);
		}
	}

	void ListenerService::executionFailed(ExecutionState &state, KInstruction *ki) {
		rdManager.getCurrentTrace()->traceType = Trace::FAILED;
	}

	void ListenerService::startControl(Executor* executor) {

		executor->executionNum++;

		BitcodeListener* PSOlistener = new PSOListener(executor, &rdManager);
		pushListener(PSOlistener);
//		BitcodeListener* Symboliclistener = new SymbolicListener(executor, &rdManager);
//		pushListener(Symboliclistener);
//		BitcodeListener* Taintlistener = new TaintListener(executor, &rdManager);
//		pushListener(Taintlistener);

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

		gettimeofday(&start, NULL);

	}

	void ListenerService::endControl(Executor* executor) {

#if DEBUG_RUNTIME
		//true: output to file; false: output to terminal
		rdManager.printCurrentTrace(true);
		//			encode.showInitTrace();//need to be modified
#endif

		if (executor->execStatus != Executor::SUCCESS) {
			llvm::errs() << "######################执行有错误,放弃本次执行##############\n";
//			executor->isFinished = true;
			executor->execStatus = Executor::SUCCESS;
//			return;
		} else if (!rdManager.isCurrentTraceUntested()) {
			rdManager.getCurrentTrace()->traceType = Trace::REDUNDANT;
			llvm::errs() << "######################本条路径为旧路径####################\n";
		} else {
			llvm::errs() << "######################本条路径为新路径####################\n";
			rdManager.getCurrentTrace()->traceType = Trace::UNIQUE;
//			Trace* trace = rdManager.getCurrentTrace();

//			unsigned allGlobal = 0;
//			std::map<std::string, std::vector<Event *> > &writeSet = trace->writeSet;
//			std::map<std::string, std::vector<Event *> > &readSet = trace->readSet;
//			for (std::map<std::string, std::vector<Event *> >::iterator nit = readSet.begin(), nie = readSet.end(); nit != nie; ++nit) {
//				allGlobal += nit->second.size();
//			}
//			for (std::map<std::string, std::vector<Event *> >::iterator nit = writeSet.begin(), nie = writeSet.end(); nit != nie; ++nit) {
//				std::string varName = nit->first;
//				if (trace->readSet.find(varName) == trace->readSet.end()) {
//					allGlobal += nit->second.size();
//				}
//			}
//			rdManager.allGlobal += allGlobal;

//			gettimeofday(&finish, NULL);
//			cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
//			rdManager.runningCost += cost;
//			rdManager.allDTAMSerialCost.push_back(cost);
//
//			gettimeofday(&start, NULL);
//			encode = new Encode(&rdManager);
//			encode->buildifAndassert();
//			if (encode->verify()) {
//				encode->check_if();
//			}
//			gettimeofday(&finish, NULL);
//			cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
//			rdManager.solvingCost += cost;

//			gettimeofday(&start, NULL);
//			dtam = new DTAM(&rdManager);
//			dtam->dtam();
//			gettimeofday(&finish, NULL);
//			cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
//			rdManager.DTAMCost += cost;
//			rdManager.allDTAMCost.push_back(cost);
//
//			gettimeofday(&start, NULL);
//			encode->PTS();
//			gettimeofday(&finish, NULL);
//			cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
//			rdManager.PTSCost += cost;
//			rdManager.allPTSCost.push_back(cost);

//			delete encode;
//			delete dtam;

		}

		executor->getNewPrefix();

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
//			delete *bit;
			bitcodeListeners.pop_back();
		}

		if (executor->executionNum >= 70) {
			executor->isFinished = true;
		}

	}

	void ListenerService::ContextSwitch(Executor* executor, ExecutionState &state) {

		Trace* trace = rdManager.getCurrentTrace();
		std::vector<Event*> path = trace->path;
		Thread* thread = state.getCurrentThread();
		Thread* SwitchThread = state.getCurrentThread();
		std::list<Thread*> queue = state.getQueue();

		switch (thread->threadState) {
			case Thread::RUNNABLE: {
				if (state.ContextSwitch < 2 && !(executor->prefix && !executor->prefix->isFinished()) && state.isGlobal) {
					std::list<Thread*>::iterator it = queue.begin();
					std::list<Thread*>::iterator ie = queue.end();
					if (queue.size() > 1) {
						it++;
						for (; it != ie; it++) {
							SwitchThread = *it;
							KInstruction *ki = SwitchThread->pc;
							Event* item = trace->createEvent(SwitchThread->threadId, ki, Event::NORMAL);
							path.push_back(item);
							stringstream ss;
							ss << "Trace" << trace->Id << "#" << item->eventId;
							Prefix* prefix = new Prefix(path, trace->createThreadPoint, ss.str(), state.ContextSwitch + 1);
//							llvm::errs() << "rdManager.addScheduleSet(prefix) state.ContextSwitch + 1　:　" << ss.str() << "\n";
							rdManager.addScheduleSet(prefix);
							path.pop_back();
							state.isGlobal = false;
						}
					}
				}
				break;
			}

			case Thread::MUTEX_BLOCKED: {
				//maybe not need;
				if (state.ContextSwitch < 2 && !(executor->prefix && !executor->prefix->isFinished()) && state.isGlobal) {
					std::list<Thread*>::iterator it = queue.begin();
					std::list<Thread*>::iterator ie = queue.end();
					if (queue.size() > 1) {
						it++;
						for (; it != ie; it++) {
							SwitchThread = *it;
							KInstruction *ki = SwitchThread->pc;
							Event* item = trace->createEvent(SwitchThread->threadId, ki, Event::NORMAL);
							path.push_back(item);
							stringstream ss;
							ss << "Trace" << trace->Id << "#" << item->eventId;
							Prefix* prefix = new Prefix(path, trace->createThreadPoint, ss.str(), state.ContextSwitch);
//							llvm::errs() << "rdManager.addScheduleSet(prefix) state.ContextSwitch　:　" << ss.str() << "\n";
							rdManager.addScheduleSet(prefix);
							path.pop_back();
							state.isGlobal = false;
						}
					}
				}
				break;
			}

			default: {
				break;
			}
		}
	}

}

#endif
