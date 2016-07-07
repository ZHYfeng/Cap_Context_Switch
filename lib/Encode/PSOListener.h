/*
 * PSOListener.h
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#ifndef PSOLISTENER_H_
#define PSOLISTENER_H_

#include <map>
#include <sstream>
#include <string>

#include "../../include/klee/Config/Version.h"
#include "../../include/klee/ExecutionState.h"
#include "../../include/klee/Expr.h"
#include "../../include/klee/util/Ref.h"
#include "../Core/AddressSpace.h"
#include "../Thread/BarrierInfo.h"
#include "../Thread/StackType.h"
#include "BitcodeListener.h"
#include "../Core/Executor.h"
#include "../Encode/RuntimeDataManager.h"

namespace klee {
	class RuntimeDataManager;
} /* namespace klee */

namespace llvm {
	class Constant;
	class ConstantExpr;
	class Function;
} /* namespace llvm */

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

namespace klee {

	class PSOListener: public BitcodeListener {
		public:
			PSOListener(Executor* executor, RuntimeDataManager* rdManager);
			virtual ~PSOListener();

			void beforeRunMethodAsMain(ExecutionState &initialState);
			void beforeExecuteInstruction(ExecutionState &state, KInstruction *ki);
			void afterExecuteInstruction(ExecutionState &state, KInstruction *ki);
			void afterRunMethodAsMain(ExecutionState &state);
			void executionFailed(ExecutionState &state, KInstruction *ki);

		private:

			Executor* executor;
			Event* lastEvent;

			std::stringstream ss;
			std::map<uint64_t, unsigned> loadRecord;
			std::map<uint64_t, unsigned> storeRecord;
			std::map<uint64_t, llvm::Type*> usedGlobalVariableRecord;
			std::map<uint64_t, BarrierInfo*> barrierRecord;

		private:
			//std::vector<string> monitoredFunction;
			void handleInitializer(llvm::Constant* initializer, MemoryObject* mo, uint64_t& startAddress);
			void handleConstantExpr(llvm::ConstantExpr* expr);
			void insertGlobalVariable(ref<Expr> address, llvm::Type* type);
			ref<Expr> getExprFromMemory(ref<Expr> address, ObjectPair& op, llvm::Type* type);
			llvm::Constant* handleFunctionReturnValue(ExecutionState& state, KInstruction *ki);
			void handleExternalFunction(ExecutionState& state, KInstruction *ki);
			void analyzeInputValue(uint64_t& address, ObjectPair& op, llvm::Type* type);
			unsigned getLoadTime(uint64_t address);
			unsigned getStoreTime(uint64_t address);
			unsigned getStoreTimeForTaint(uint64_t address);
			llvm::Function* getPointeredFunction(ExecutionState& state, KInstruction* ki);

			std::string createVarName(unsigned memoryId, ref<Expr> address, bool isGlobal) {
				char signal;
				ss.str("");
				if (isGlobal) {
					signal = 'G';
				} else {
					signal = 'L';
				}
				ss << signal;
				ss << memoryId;
				ss << '_';
				ss << address;
				return ss.str();
			}
			std::string createVarName(unsigned memoryId, uint64_t address, bool isGlobal) {
				char signal;
				ss.str("");
				if (isGlobal) {
					signal = 'G';
				} else {
					signal = 'L';
				}
				ss << signal;
				ss << memoryId;
				ss << '_';
				ss << address;
				return ss.str();
			}

			std::string createGlobalVarFullName(std::string varName, unsigned time, bool isStore) {
				char signal;
				ss.str("");
				ss << varName;
				if (isStore) {
					signal = 'S';
				} else {
					signal = 'L';
				}
				ss << signal;
				ss << time;
				return ss.str();
			}

			std::string createBarrierName(uint64_t address, unsigned releasedCount) {
				ss.str("");
				ss << address;
				ss << "#";
				ss << releasedCount;
				return ss.str();
			}

	};

}

#endif /* PSOLISTENER_H_ */
