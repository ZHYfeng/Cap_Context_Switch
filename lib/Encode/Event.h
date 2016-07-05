/*
 * Event.h
 *
 *  Created on: May 29, 2014
 *      Author: ylc
 */

#ifndef EVENT_H_
#define EVENT_H_

#include "klee/Expr.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/util/Ref.h"

#include <llvm/IR/Function.h>

#include <string>
#include <vector>

namespace klee {

	class Event {
		public:
			enum EventType {
				NORMAL, //which participates in the encode and has associated inst
				IGNORE, //which does not participate in the encode
				VIRTUAL //which has no associated inst
			};
		public:
			unsigned threadId;
			unsigned eventId;
			unsigned threadEventId;
			std::string eventName;
			KInstruction* inst;
			std::string name; //name of load or store variable
			std::string globalName; //globalVarName + the read / write sequence
			EventType eventType;
			Event * latestWriteEventInSameThread;
			bool isGlobal; // is global variable  load, store, call strcpy in these three instruction this attribute will be assigned
			bool isEventRelatedToBranch;
			bool isConditionInst; // is this event associated with a Br which has two targets
			bool brCondition; // Br's condition
			bool isFunctionWithSourceCode; // only use by call, whether the called function is defined by user
			llvm::Function* calledFunction; //set for called function. all callinst use it.@14.12.02
			std::vector<unsigned> vectorClock;
			std::vector<ref<klee::Expr> > instParameter;
			std::vector<ref<klee::Expr> > relatedSymbolicExpr;

			Event();
			Event(unsigned threadId, unsigned eventId, std::string eventName, KInstruction* inst, std::string globalVarName,
					std::string globalVarFullName, EventType eventType);
			virtual ~Event();
			std::string toString();

	};

} /* namespace klee */

#endif /* EVENT_H_ */
