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

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			(*bit)->beforeRunMethodAsMain(state);

		}
	}

	void ListenerService::beforeExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki) {

#if DEBUG_RUNTIME
		llvm::errs() << "thread id : " << state.currentThread->threadId << "  ";
		ki->inst->dump();
//		llvm::errs() << " before : " << "prefix : " << executor->prefix->isFinished() << "\n";
#endif

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			(*bit)->beforeExecuteInstruction(state, ki);

		}
	}

	void ListenerService::afterExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki) {

//		llvm::errs() << " after : " << "\n";

		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {

			(*bit)->afterExecuteInstruction(state, ki);

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
			llvm::errs() << "\n######################执行有错误,放弃本次执行##############\n";
//			executor->isFinished = true;
			executor->execStatus = Executor::SUCCESS;
//			return;
		} else if (!rdManager.isCurrentTraceUntested()) {
			rdManager.getCurrentTrace()->traceType = Trace::REDUNDANT;
			llvm::errs() << "\n######################本条路径为旧路径####################\n";
		} else {
			llvm::errs() << "\n######################本条路径为新路径####################\n";
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
			delete *bit;
//			(*bit)->~BitcodeListener();
			bitcodeListeners.pop_back();
		}

//		if (executor->executionNum >= 70) {
//			executor->isFinished = true;
//		}

	}

	void ListenerService::ContextSwitch(Executor* executor, ExecutionState &state) {

//		llvm::errs() << "ContextSwitch\n";
		Trace* trace = rdManager.getCurrentTrace();
		std::vector<Event*> path = trace->path;
		Thread* thread = state.getCurrentThread();
		Thread* SwitchThread = state.getCurrentThread();
		std::list<Thread*> queue = state.getQueue();
//		KInstruction *ki = thread->pc;

//		llvm::errs() << "ContextSwitch thread id : " << thread->threadId << "  ";
//		ki->inst->dump();

//		llvm::errs() << "state.ContextSwitch : " << state.ContextSwitch << "\n";
//		llvm::errs() << "(!(executor->prefix && !executor->prefix->isFinished())) : " << (!(executor->prefix && !executor->prefix->isFinished())) << "\n";
//		llvm::errs() << "state.isGlobal : " << state.isGlobal << "\n";

		switch (thread->threadState) {
			case Thread::RUNNABLE: {
				if (state.ContextSwitch < 2 && (!(executor->prefix && !executor->prefix->isFinished())) && state.isGlobal) {
					std::list<Thread*>::iterator it = queue.begin();
					std::list<Thread*>::iterator ie = queue.end();
					if (queue.size() > 1) {
						it++;
						for (; it != ie; it++) {
							SwitchThread = *it;
							KInstruction *ki = SwitchThread->pc;
//							llvm::errs() << "RUNNABLE SwitchThread id : " << SwitchThread->threadId;
							Event* item = trace->createEvent(SwitchThread->threadId, ki, Event::NORMAL);
							path.push_back(item);
							stringstream ss;
							ss << "Trace" << trace->Id << "#" << item->eventId;
							Prefix* prefix = new Prefix(path, trace->createThreadPoint, ss.str(), state.ContextSwitch + 1);
//							llvm::errs() << "rdManager.addScheduleSet(prefix) state.ContextSwitch + 1　:　" << ss.str() << "\n";
							rdManager.addScheduleSet(prefix);
							path.pop_back();
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
//							llvm::errs() << "MUTEX_BLOCKED SwitchThread id : " << SwitchThread->threadId << "\n";
							KInstruction *ki = SwitchThread->pc;
							Event* item = trace->createEvent(SwitchThread->threadId, ki, Event::NORMAL);
							path.push_back(item);
							stringstream ss;
							ss << "Trace" << trace->Id << "#" << item->eventId;
							Prefix* prefix = new Prefix(path, trace->createThreadPoint, ss.str(), state.ContextSwitch);
//							llvm::errs() << "rdManager.addScheduleSet(prefix) state.ContextSwitch　:　" << ss.str() << "\n";
							rdManager.addScheduleSet(prefix);
							path.pop_back();
						}
					}
				}
				break;
			}

			default: {
				break;
			}
		}
		if(state.isGlobal){
			state.isGlobal = false;
		}
	}

}

#endif
