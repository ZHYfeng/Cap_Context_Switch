/*
 * Transfer.h
 *
 *  Created on: Jul 28, 2014
 *      Author: ylc
 */

#ifndef TRANSFER_H_
#define TRANSFER_H_

#include "klee/Expr.h"
#include "klee/Config/Version.h"

#include "llvm/Support/DataTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

#include <string>
#include <sstream>

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
