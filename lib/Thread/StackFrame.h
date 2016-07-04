/*
 * StackFrame.h
 *
 *  Created on: Jun 14, 2016
 *      Author: zhy
 */

#ifndef LIB_THREAD_STACKFRAME_H_
#define LIB_THREAD_STACKFRAME_H_

#include <vector>

#include "../../include/klee/Internal/Module/KInstIterator.h"
#include "../Core/Memory.h"
#include "../../include/klee/Internal/Module/Cell.h"

namespace klee {
class CallPathNode;
struct Cell;
struct KFunction;
class MemoryObject;
} /* namespace klee */

namespace klee {

class StackFrame {
public:
	KInstIterator caller;
	KFunction *kf;
	CallPathNode *callPathNode;
	std::vector<const MemoryObject *> allocas;
	Cell *locals;

	/// Minimum distance to an uncovered instruction once the function
	/// returns. This is not a good place for this but is used to
	/// quickly compute the context sensitive minimum distance to an
	/// uncovered instruction. This value is updated by the StatsTracker
	/// periodically.
	unsigned minDistToUncoveredOnReturn;

	// For vararg functions: arguments not passed via parameter are
	// stored (packed tightly) in a local (alloca) memory object. This
	// is setup to match the way the front-end generates vaarg code (it
	// does not pass vaarg through as expected). VACopy is lowered inside
	// of intrinsic lowering.
	MemoryObject *varargs;

	StackFrame(KInstIterator caller, KFunction *kf);
	StackFrame(const StackFrame &s);
	virtual ~StackFrame();
};
} /* namespace klee */

#endif /* LIB_THREAD_STACKFRAME_H_ */
