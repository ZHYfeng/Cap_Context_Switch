/*
 * Transfer.h
 *
 *  Created on: Jul 28, 2014
 *      Author: ylc
 */

#ifndef TRANSFER_H_
#define TRANSFER_H_

#include <string>
#include <sstream>
#include "llvm/Support/DataTypes.h"
#include "klee/Config/Version.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#else
#include "llvm/Instruction.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#endif
#include "klee/Expr.h"

namespace klee {

class Transfer {
private:
	static std::stringstream ss;
public:
	static std::string uint64toString(uint64_t input);
	static llvm::Constant* expr2Constant(klee::Expr* expr, llvm::Type* type);
	Transfer();
	virtual ~Transfer();
};

} /* namespace klee */

#endif /* TRANSFER_H_ */
