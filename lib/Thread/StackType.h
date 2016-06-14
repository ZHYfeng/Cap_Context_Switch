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

#include "../../include/klee/Internal/Module/KInstIterator.h"
#include "StackFrame.h"

namespace klee {
struct KFunction;
} /* namespace klee */

namespace klee {

class StackType {
public:
	StackType(AddressSpace *addressSpace);
	virtual ~StackType();

	void pushFrame(KInstIterator caller, KFunction *kf);
	void popFrame();
	void dumpStack(std::ostream &out, KInstruction *prevPC) const;

public:
	std::vector<StackFrame> realStack;
	AddressSpace *addressSpace;
};

} /* namespace klee */

#endif /* LIB_THREAD_STACKTYPE_H_ */
