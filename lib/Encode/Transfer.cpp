/*
 * Transfer.cpp
 *
 *  Created on: Jul 28, 2014
 *      Author: ylc
 */

#include "Transfer.h"

#include "../Core/Context.h"

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/Casting.h>

using namespace std;
using namespace llvm;

namespace klee {

	std::stringstream Transfer::ss;

	string Transfer::uint64toString(uint64_t input) {
		ss.str("");
		ss << input;
		return ss.str();
	}

	/**
	 * 将ConstantExpr转换为对应的Constant类型
	 */
	Constant* Transfer::expr2Constant(Expr* expr, Type* type) {
		Constant* param = NULL;
		if (type->isIntegerTy()) {
			ConstantExpr* constantExpr = dyn_cast<ConstantExpr>(expr);
			param = ConstantInt::get(type, constantExpr->getAPValue());
		} else if (type->isFloatTy()) {
			ConstantExpr* constantExpr = dyn_cast<ConstantExpr>(expr);
			APFloat apValue(APFloat::IEEEsingle, constantExpr->getAPValue());
			param = ConstantFP::get(type->getContext(), apValue);
		} else if (type->isDoubleTy()) {
			ConstantExpr* constantExpr = dyn_cast<ConstantExpr>(expr);
			APFloat apValue(APFloat::IEEEdouble, constantExpr->getAPValue());
			param = ConstantFP::get(type->getContext(), apValue);
		} else if (type->isPointerTy()) {
			ConstantExpr* constantExpr = dyn_cast<ConstantExpr>(expr);
			param = ConstantInt::get(Type::getIntNTy(type->getContext(), Context::get().getPointerWidth()), constantExpr->getAPValue());
		} else {
			assert(0 && "not support type");
		}
		return param;
	}

	Transfer::Transfer() {

	}

	Transfer::~Transfer() {

	}

} /* namespace klee */
