/*
 * StackType.h
 *
 *  Created on: Jun 14, 2016
 *      Author: zhy
 */

#ifndef LIB_THREAD_STACKTYPE_H_
#define LIB_THREAD_STACKTYPE_H_

#include <iostream>
#include <vector>

#include "klee/Internal/Module/KInstIterator.h"
#include "StackFrame.h"
#include "../Core/AddressSpace.h"
#include "klee/Internal/Module/KModule.h"

namespace klee {

	class StackType {
		public:
			StackType(AddressSpace *addressSpace);
			StackType(AddressSpace *addressSpace, StackType *stack);
			virtual ~StackType();

			void pushFrame(KInstIterator caller, KFunction *kf);
			void popFrame();
			void dumpStack(llvm::raw_ostream &out, KInstIterator prevPC) const;

		public:
			std::vector<StackFrame> realStack;
			AddressSpace *addressSpace;
	};

} /* namespace klee */

#endif /* LIB_THREAD_STACKTYPE_H_ */
