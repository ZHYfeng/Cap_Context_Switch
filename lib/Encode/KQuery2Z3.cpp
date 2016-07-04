/***--KQuery2Z3.cpp--**********************************************
 *
 * The implementation for KQuery2Z3.h,complete convert kquery to z3 expr
 *
 *
 */

#include "KQuery2Z3.h"

#include "klee/Expr.h"
#include "llvm/ADT/APFloat.h"
#include <math.h>
//#include "llvm/Support/Casting.h"

//
//#include <z3.h>
//#include <z3_api.h>
//#include <z3++.h>
//#include <assert.h>
//#include <vector>
//#include <iostream>
//#include <sstream>

using namespace klee;
using namespace z3;

//we also use namespace std, z3

#define EPSILON 0.00001
#define BIT_WIDTH 64
#define INT_ARITHMETIC 0

//constructor
KQuery2Z3::KQuery2Z3(std::vector<ref<Expr> > &_kqueryExpr, z3::context& _z3_ctx) :
		kqueryExpr(_kqueryExpr), z3_ctx(_z3_ctx) {

}

KQuery2Z3::KQuery2Z3(z3::context& _z3_ctx) :
		z3_ctx(_z3_ctx) {

}

KQuery2Z3::~KQuery2Z3() {

}

int KQuery2Z3::validNum(std::string& str) {
	//get the valid number after dot notation of a float point.
	int len = str.length();
	int start = 0;
	int end = len;
	int res = 0;
	for (int i = 0; i < len; i++) {
		if (str[i] == '.') {
			start = i;
			break;
		}
	}
	for (int i = (len - 1); i >= 0; i--) {
		if (str[i] != '0' || str[i] == '.') {
			end = i;
			break;
		}
	}
	res = end - start;
	return res;
}

void KQuery2Z3::getFraction(double dvar, Fraction& frac) {
	//get double value's numerator and denomerator
	//this implementation could crash when the valid number after the dot
	//too many,like .1234545677788.... that makes temp overflow,must fix.
	int numerator = (int) dvar;
	double left = dvar - (double) numerator;
	int denomerator = 0;
	std::stringstream ss;
	ss << left;
	std::string str = ss.str();
	int times = validNum(str);
	int temp = 1;
	for (int i = 0; i < times; i++) {
		temp *= 10;
	}

	numerator = (int) (dvar * temp);
	denomerator = temp;
	frac.num = numerator;
	frac.den = denomerator;
}

void KQuery2Z3::getZ3Expr() {
//	std::cerr << "execute in getZ3Expr\n";
//	std::cerr << "size = " << kqueryExpr.size() << std::endl;
	std::vector<ref<Expr> >::iterator biter = kqueryExpr.begin();
	std::vector<ref<Expr> >::iterator eiter = kqueryExpr.end();
	for (; biter != eiter; biter++) {
//		biter->get()->dump();
		vecZ3Expr.push_back(eachExprToZ3(*biter));
	}
}

z3::expr KQuery2Z3::eachExprToZ3(ref<Expr> &ele) {
	z3::expr res = z3_ctx.bool_val(true);

	switch (ele->getKind()) {

	case Expr::Constant: {
		ConstantExpr *ce = cast<ConstantExpr>(ele);
		Expr::Width width = ce->getWidth();

		if (ele.get()->isFloat) {
			//float point number
			//in z3 there is no difference between float and double
			//they are all real value.
			double temp = 1.0;
			if (width == 32) {
				llvm::APFloat resValue(llvm::APFloat::IEEEsingle,
						ce->getAPValue());
				temp = resValue.convertToFloat(); //the real float number,how to establish expr of z3
			} else if (width == 64) {
				llvm::APFloat resValue(llvm::APFloat::IEEEdouble,
						ce->getAPValue());
				temp = resValue.convertToDouble(); // the real double number.
			}
			Fraction frac;
			getFraction(temp, frac);
//			std::stringstream ss;
//			ss << temp;
//				std::cerr << "frac.num = " << frac.num << " "
//						<< "frac.den = " << frac.den << std::endl;
//			res = z3_ctx.real_val(ss.str().c_str());
			res = z3_ctx.real_val(frac.num, frac.den);
//				std::cerr << "float point value = " << res << std::endl;
		} else {
			if (width == Expr::Bool) {
				if (ce->isTrue()) {
					//that is true
					res = z3_ctx.bool_val(true);
				} else {
					//that is false
					res = z3_ctx.bool_val(false);
				}
			} else if (width != Expr::Fl80) {
				int temp = ce->getZExtValue();
//					std::cerr << "temp = " << temp << std::endl;
#if INT_ARITHMETIC
				res = z3_ctx.int_val(temp);
#else
				res = z3_ctx.bv_val(temp, BIT_WIDTH);
#endif

//	     			std::cerr << res;
			} else {
				assert(0 && "The Fl80 out, value bit number extends 64");
			}
		}
		return res;
	}

	case Expr::NotOptimized: {
		assert(0 && "don't handle NotOptimized expression");
		return res;
	}

	case Expr::Read: {
		//type char
		ReadExpr *re = cast<ReadExpr>(ele);
		assert(re && re->updates.root);
		const std::string varName = re->updates.root->name;
		if (re->getWidth() == Expr::Bool) {
			res = z3_ctx.bool_const(varName.c_str());
		} else {
#if INT_ARITHMETIC
				res = z3_ctx.constant(varName.c_str(), z3_ctx.int_sort());
#else
				res = z3_ctx.constant(varName.c_str(), z3_ctx.bv_sort(BIT_WIDTH));
#endif

		}
		return res;
	}

	case Expr::Select: {
		SelectExpr *se = cast<SelectExpr>(ele);

		z3::expr cond = eachExprToZ3(se->cond);
		z3::expr tExpr = eachExprToZ3(se->trueExpr);
		z3::expr fExpr = eachExprToZ3(se->falseExpr);

		res = z3::ite(cond, tExpr, fExpr);
		return res;
	}

	case Expr::Concat: {
		ConcatExpr *ce = cast<ConcatExpr>(ele);
		ReadExpr *re = NULL;
		if (ce->getKid(0)->getKind() == Expr::Read)
			re = cast<ReadExpr>(ce->getKid(0));
		else if (ce->getKid(1)->getKind() == Expr::Read)
			re = cast<ReadExpr>(ce->getKid(1));
		else
			assert("file: kQuery2z3, Expr::Concat" && false);
		const std::string varName = re->updates.root->name;
		if (ce->isFloat) {  //float point symbolics
//				std::cerr << "build float " << varName << std::endl;
			res = z3_ctx.constant(varName.c_str(), z3_ctx.real_sort());
		} else {

//			std::cerr << "build bitvector" << varName << std::endl;
			if (ele.get()->isFloat)
				res = z3_ctx.constant(varName.c_str(), z3_ctx.real_sort());
			else
#if INT_ARITHMETIC
				res = z3_ctx.constant(varName.c_str(), z3_ctx.int_sort());
#else
				res = z3_ctx.constant(varName.c_str(), z3_ctx.bv_sort(BIT_WIDTH));
#endif

		}
//			else {
//				assert("real concat operation happened in Expr::Concat\n"
//						"need to be handle in minutes");
//			}
		return res;
	}

		//Trunc and FPTrunc and FPtoSI and FPtoUI
	case Expr::Extract: {
		ExtractExpr * ee = cast<ExtractExpr>(ele);
		z3::expr src = eachExprToZ3(ee->expr);

//			std::cerr << "print in the Extract in kquery2z3\n";
//			ele->dump();
//			ee->expr.get()->dump();

		//convert to boolean value, that means the result
		//depends on the ExtractExpr's least significant
		//bytes.
		if (ee->expr.get()->isFloat && !ee->isFloat) {
			//handle fptosi and fptoui
//			std::cerr << "handle fptosi \n";
//			ele.get()->dump();
			try {
#if INT_ARITHMETIC
			z3::expr temp = z3::to_expr(z3_ctx, Z3_mk_real2int(z3_ctx, src));
#else
			z3::expr temp = z3::to_expr(z3_ctx, Z3_mk_real2int(z3_ctx, src));
			z3::expr vecTemp = z3::to_expr(z3_ctx,
					Z3_mk_int2bv(z3_ctx, BIT_WIDTH, temp));
#endif
			if (ee->width == Expr::Bool) {
				//handle double->bool the special
#if INT_ARITHMETIC
				res = z3::ite(temp, z3_ctx.bool_val(1), z3_ctx.bool_val(0));
#else
				res = z3::ite(
						z3::to_expr(z3_ctx,
								Z3_mk_extract(z3_ctx, 0, 0, vecTemp)),
								z3_ctx.bv_val(1, BIT_WIDTH), z3_ctx.bv_val(0, BIT_WIDTH));
#endif
			} else {
#if INT_ARITHMETIC
				res = temp;
#else
				res = vecTemp;
#endif
			}
			} catch (z3::exception &ex) {
				std::cerr << "exception = " << ex << std::endl;
			}
		} else if (!ee->expr.get()->isFloat && !ee->isFloat) {
			//handle trunc and fptrunc, both these instructions
			//have same type before or after convert.
			if (ee->width == Expr::Bool) {
				//handle int->bool the special
#if INT_ARITHMETIC
				res = z3::ite(src, z3_ctx.int_val(1), z3_ctx.int_val(0));
#else
				res = z3::ite(
						z3::to_expr(z3_ctx, Z3_mk_extract(z3_ctx, 0, 0, src)),
						z3_ctx.bv_val(1, BIT_WIDTH), z3_ctx.bv_val(0, BIT_WIDTH));
#endif

			} else {
				res = src;
			}
		} else {
			//float->float;
			res = src;
		}
		return res;
	}

		//ZExt SExt handle methods from encode.cpp
	case Expr::ZExt: {
		CastExpr * ce = cast<CastExpr>(ele);
		z3::expr src = eachExprToZ3(ce->src);

		if (ce->src.get()->getWidth() == Expr::Bool) {
#if INT_ARITHMETIC
				res = z3::ite(src, z3_ctx.int_val(1), z3_ctx.int_val(0));
#else
				res = z3::ite(src, z3_ctx.bv_val(1, BIT_WIDTH), z3_ctx.bv_val(0, BIT_WIDTH));
//				res = z3::ite(
//					z3::to_expr(z3_ctx, Z3_mk_extract(z3_ctx, 0, 0, src)),
//					z3_ctx.bool_val(true), z3_ctx.bool_val(false));
#endif
//			if (Z3_TRUE == Z3_algebraic_is_zero(z3_ctx, src)) {
//				res = z3_ctx.bool_val(false);
//			} else {
//				res = z3_ctx.bool_val(true);
//			}
		} else {
			res = src;
		}
		return res;
	}

	case Expr::SExt: {
		CastExpr * ce = cast<CastExpr>(ele);

		//convert int or unsigned int to float point
		//need to handle alone.
//			if (ce->isFloat) {
//				res = eachExprToZ3(ce->src, true);
//			} else {
		z3::expr src = eachExprToZ3(ce->src);
		if (ce->isFloat && !ce->src.get()->isFloat) {
			try {
#if INT_ARITHMETIC
			z3::expr realTemp = to_expr(z3_ctx, Z3_mk_int2real(z3_ctx, src));

#else
			z3::expr temp = to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, src, true));
			z3::expr realTemp = to_expr(z3_ctx, Z3_mk_int2real(z3_ctx, temp));
#endif
			res = realTemp;
			} catch (z3::exception &ex) {
				std::cerr << "exception = " << ex << std::endl;
			}
		} else if (!ce->isFloat && !ce->src.get()->isFloat) {
			if (ce->src.get()->getWidth() == Expr::Bool
					&& ce->width != Expr::Bool) {
#if INT_ARITHMETIC
				res = z3::ite(src, z3_ctx.int_val(1), z3_ctx.int_val(0));
#else
				res = z3::ite(src, z3_ctx.bv_val(1, BIT_WIDTH), z3_ctx.bv_val(0, BIT_WIDTH));
#endif
//				res = z3::ite(src, z3_ctx.bool_val(true), z3_ctx.bool_val(false));
//				res = z3::ite(
//						z3::to_expr(z3_ctx, Z3_mk_extract(z3_ctx, 0, 0, src)),
//						z3_ctx.bool_val(true), z3_ctx.bool_val(false));
			} else {
				res = src;
			}
		} else {
			res = src;
		}
//			}
		return res;
	}

	case Expr::Add: {
		AddExpr *ae = cast<AddExpr>(ele);
//			std::cerr << "Is this wrong?\n";
//			ae->left.get()->dump();
//			ae->right.get()->dump();
//			std::cerr << "Add expr show\n";
//			ele.get()->dump();


			// if one of the operand is a float point number
			// then the left and right are all float point number.
			if (ae->left.get()->isFloat || ae->right.get()->isFloat) {
				ae->left.get()->isFloat = true;
				ae->right.get()->isFloat = true;
			}
		z3::expr left = eachExprToZ3(ae->left);
		z3::expr right = eachExprToZ3(ae->right);

//		assert(
//				left.get_sort() == right.get_sort()
//						&& "sort between left and right are different in Expr::Add\n");

			res = left + right;
			return res;
		}

		case Expr::Sub: {
			SubExpr *se = cast<SubExpr>(ele);
			if (se->left.get()->isFloat || se->right.get()->isFloat) {
				se->left.get()->isFloat = true;
				se->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(se->left);
			z3::expr right = eachExprToZ3(se->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Sub\n");

		res = left - right;
		return res;
	}

		case Expr::Mul: {
			MulExpr *me = cast<MulExpr>(ele);
			if (me->left.get()->isFloat || me->right.get()->isFloat) {
				me->left.get()->isFloat = true;
				me->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(me->left);
			z3::expr right = eachExprToZ3(me->right);
//			std::cerr << left << "\n";
//			std::cerr << right << "\n";
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Mul\n");


		res = left * right;
		return res;
	}

	case Expr::UDiv: {
		//could handled with SDiv, but for test just do in here.
			UDivExpr *ue = cast<UDivExpr>(ele);
			if (ue->left.get()->isFloat || ue->right.get()->isFloat) {
				ue->left.get()->isFloat = true;
				ue->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ue->left);
			z3::expr right = eachExprToZ3(ue->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::UDiv\n");
		try {
			if (left.is_bv()) {
				res = z3::to_expr(z3_ctx, Z3_mk_bvudiv(z3_ctx, left, right));
			} else {
				res = z3::to_expr(z3_ctx, Z3_mk_div(z3_ctx, left, right));
			}
		} catch (z3::exception &ex) {
				std::cerr << "UDiv exception = " << ex << std::endl;
		}
		return res;
	}

		case Expr::SDiv: {
			SDivExpr *se = cast<SDivExpr>(ele);
			if (se->left.get()->isFloat || se->right.get()->isFloat) {
				se->left.get()->isFloat = true;
				se->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(se->left);
			z3::expr right = eachExprToZ3(se->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::SDiv\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvsdiv(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_div(z3_ctx, left, right));
				}
			} catch (z3::exception &ex) {
				std::cerr << "SDiv exception = " << ex << std::endl;
			}

		return res;
	}

		case Expr::URem: {
			URemExpr *ur = cast<URemExpr>(ele);
			if (ur->left.get()->isFloat || ur->right.get()->isFloat) {
				ur->left.get()->isFloat = true;
				ur->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ur->left);
			z3::expr right = eachExprToZ3(ur->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::URem\n");
			try {
				if (left.is_bv()) {
					//bitvecotor, all int are bitvector;
					res = z3::to_expr(z3_ctx, Z3_mk_bvurem(z3_ctx, left, right));
				} else {
				//float
					res = z3::to_expr(z3_ctx, Z3_mk_rem(z3_ctx, left, right));
				}
			} catch (z3::exception &ex) {
				std::cerr << "URem exception = " << ex << std::endl;
			}
			return res;
		}

		case Expr::SRem: {
			SRemExpr *sr = cast<SRemExpr>(ele);
			if (sr->left.get()->isFloat || sr->right.get()->isFloat) {
				sr->left.get()->isFloat = true;
				sr->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(sr->left);
			z3::expr right = eachExprToZ3(sr->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::SRem\n");
			try {
				if (left.is_bv()) {
					//bitvecotor, all int are bitvector;
					res = z3::to_expr(z3_ctx, Z3_mk_bvsrem(z3_ctx, left, right));
				} else {
					//float
					res = z3::to_expr(z3_ctx, Z3_mk_rem(z3_ctx, left, right));
				}
			} catch (z3::exception &ex) {
				std::cerr << "SRem exception = " << ex << std::endl;
			}
			return res;
		}

	case Expr::Not: {
		NotExpr *ne = cast<NotExpr>(ele);
		res = eachExprToZ3(ne->expr);
		try {
			if (res.is_bv()) {
				res = z3::to_expr(z3_ctx, Z3_mk_bvnot(z3_ctx, res));
			} else {
				res = z3::to_expr(z3_ctx, Z3_mk_not(z3_ctx, res));
			}
		} catch (z3::exception &ex) {
			std::cerr << "Not exception = " << ex << std::endl;
		}
		return res;
	}

		case Expr::And: {
			AndExpr *ae = cast<AndExpr>(ele);
			if (ae->left.get()->isFloat || ae->right.get()->isFloat) {
				ae->left.get()->isFloat = true;
				ae->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ae->left);
			z3::expr right = eachExprToZ3(ae->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::And\n");
//			std::cerr << "And left = " << left << std::endl;
//			std::cerr << "And right = " << right << std::endl;
			if (left.is_bv()) {
				res = z3::to_expr(z3_ctx, Z3_mk_bvand(z3_ctx, left, right));
			} else {
//				std::cerr << "left = " << left << ", left sort = " << left.get_sort() << std::endl;
//				std::cerr << "right = " << right << ", right sort = " << right.get_sort() << std::endl;
#if INT_ARITHMETIC
				try {
					if (left.is_int()) {
						z3::expr tempLeft = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, left));
//						std::cerr << "tempLeft = " << tempLeft << std::endl;
						z3::expr tempRight = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, right));
//						std::cerr << "tempRight = " << tempRight << std::endl;

						z3::expr tempRes = z3::to_expr(z3_ctx, Z3_mk_bvand(z3_ctx, tempLeft, tempRight));
//						std::cerr << "tempRes = " << tempRes << std::endl;
						res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
//						std::cerr << "res = " << res << std::endl;
					} else {
						res = left && right;
					}
				} catch (z3::exception &ex) {
					std::cerr << "And exception = " << ex << std::endl;
				}
#else
				res = left && right;
#endif
			}
		return res;
	}


		case Expr::Or: {
			OrExpr *oe = cast<OrExpr>(ele);
			if (oe->left.get()->isFloat || oe->right.get()->isFloat) {
				oe->left.get()->isFloat = true;
				oe->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(oe->left);
			z3::expr right = eachExprToZ3(oe->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Or\n");

			if (left.is_bv()) {
				res = z3::to_expr(z3_ctx, Z3_mk_bvor(z3_ctx, left, right));
			} else {
#if INT_ARITHMETIC
				try {
					if (left.is_int()) {
						z3::expr tempLeft = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, left));
						z3::expr tempRight = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, right));
						z3::expr tempRes = z3::to_expr(z3_ctx, Z3_mk_bvor(z3_ctx, tempLeft, tempRight));
						res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
					} else {
						res = left || right;
					}
				} catch(z3::exception &ex) {
					std::cerr << "Or exception : " << ex << std::endl;
				}
#else
				res = left || right;
#endif
			}
		return res;
	}


		case Expr::Xor: {
			XorExpr *xe = cast<XorExpr>(ele);
			if (xe->left.get()->isFloat || xe->right.get()->isFloat) {
				xe->left.get()->isFloat = true;
				xe->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(xe->left);
			z3::expr right = eachExprToZ3(xe->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Xor\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvxor(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_xor(z3_ctx, left, right));
				}
			} catch(z3::exception &ex) {
				std::cerr << "Xor exception : " << ex << std::endl;
			}
		return res;
	}

		//following shift operation must be bitvector operand.
	case Expr::Shl: {
		ShlExpr * se = cast<ShlExpr>(ele);
		z3::expr left = eachExprToZ3(se->left);
		z3::expr right = eachExprToZ3(se->right);
		//convert bit vector to int in order to binary operation.
//		assert(
//				left.get_sort() == right.get_sort()
//						&& "sort between left and right are different in Expr::Shl\n");
#if INT_ARITHMETIC
		try {
			z3::expr tempLeft = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, left));
			z3::expr tempRight = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, right));

			z3::expr tempRes = z3::to_expr(z3_ctx, Z3_mk_bvshl(z3_ctx, tempLeft, tempRight));
			res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
		} catch(z3::exception &ex) {
			std::cerr << "Shl exception : " << ex << std::endl;
		}
#else
		res = z3::to_expr(z3_ctx, Z3_mk_bvshl(z3_ctx, left, right));
//		res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
#endif


		return res;
	}

	case Expr::LShr: {
		LShrExpr * lse = cast<LShrExpr>(ele);
		z3::expr left = eachExprToZ3(lse->left);
		z3::expr right = eachExprToZ3(lse->right);
//		assert(
//				left.get_sort() == right.get_sort()
//						&& "sort between left and right are different in Expr::LShr\n");
#if INT_ARITHMETIC
		try {
			z3::expr tempLeft = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, left));
			z3::expr tempRight = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, right));
			z3::expr tempRes = z3::to_expr(z3_ctx, Z3_mk_bvlshr(z3_ctx, tempLeft, tempRight));
			res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
		} catch(z3::exception &ex) {
			std::cerr << "LShr exception : " << ex << std::endl;
		}
#else
		res = z3::to_expr(z3_ctx, Z3_mk_bvlshr(z3_ctx, left, right));
//		res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
#endif

		return res;
	}

	case Expr::AShr: {
		AShrExpr * ase = cast<AShrExpr>(ele);
		z3::expr left = eachExprToZ3(ase->left);
		z3::expr right = eachExprToZ3(ase->right);
//		assert(
//				left.get_sort() == right.get_sort()
//						&& "sort between left and right are different in Expr::AShr\n");
#if INT_ARITHMETIC
		try {
			z3::expr tempLeft = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, left));
			z3::expr tempRight = z3::to_expr(z3_ctx, Z3_mk_int2bv(z3_ctx, BIT_WIDTH, right));
			z3::expr tempRes = z3::to_expr(z3_ctx, Z3_mk_bvashr(z3_ctx, tempLeft, tempRight));
			res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
		} catch(z3::exception &ex) {
			std::cerr << "AShr exception : " << ex << std::endl;
		}
#else
		res = z3::to_expr(z3_ctx, Z3_mk_bvashr(z3_ctx, left, right));
//		res = z3::to_expr(z3_ctx, Z3_mk_bv2int(z3_ctx, tempRes, true));
#endif

		return res;
	}

		case Expr::Eq: {
			EqExpr *ee = cast<EqExpr>(ele);
			if (ee->left.get()->isFloat || ee->right.get()->isFloat) {
				ee->left.get()->isFloat = true;
				ee->right.get()->isFloat = true;
			}
//			std::cerr << "ele = " << ele << std::endl;
			z3::expr left = eachExprToZ3(ee->left);
//			std::cerr << "left = " << left << std::endl;
			z3::expr right = eachExprToZ3(ee->right);
//			assert(
//					Z3_get_sort_kind(z3_ctx, left)
//							&& "sort between left and right are different in Expr::Eq\n");
//			std::cerr << "right = " << right << std::endl;
//			std::cerr << "ee left = " << ee->left << std::endl;
//			std::cerr << "ee right = " << ee->right << std::endl;
//			std::cerr << "left = " << left << ", left sort = " << left.get_sort() << std::endl;
//			std::cerr << "right = " << right << ", right sort = " << right.get_sort() << std::endl;
			res = (left == right);

			return res;
		}

		case Expr::Ult: {
			//probably can float point value's comparison.
			UltExpr *ue = cast<UltExpr>(ele);
			if (ue->left.get()->isFloat || ue->right.get()->isFloat) {
				ue->left.get()->isFloat = true;
				ue->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ue->left);
			z3::expr right = eachExprToZ3(ue->right);
//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Ult\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvult(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_lt(z3_ctx, left, right));
				}
			} catch(z3::exception &ex) {
				std::cerr << "Ult exception : " << ex << std::endl;
			}
			return res;
		}


		case Expr::Ule: {
			UleExpr *ue = cast<UleExpr>(ele);
			if (ue->left.get()->isFloat || ue->right.get()->isFloat) {
							ue->left.get()->isFloat = true;
							ue->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ue->left);
			z3::expr right = eachExprToZ3(ue->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Ule\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvule(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_le(z3_ctx, left, right));
				}
			} catch(z3::exception &ex) {
				std::cerr << "Ule exception : " << ex << std::endl;
			}
		return res;
	}
		case Expr::Slt: {
			SltExpr *se = cast<SltExpr>(ele);
			if (se->left.get()->isFloat || se->right.get()->isFloat) {
				se->left.get()->isFloat = true;
				se->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(se->left);
			z3::expr right = eachExprToZ3(se->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Slt\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvslt(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_lt(z3_ctx, left, right));
				}
			} catch(z3::exception &ex) {
				std::cerr << "Slt exception : " << ex << std::endl;
			}
		return res;
	}
		case Expr::Sle: {
			SleExpr *se = cast<SleExpr>(ele);
			if (se->left.get()->isFloat || se->right.get()->isFloat) {
							se->left.get()->isFloat = true;
							se->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(se->left);
			z3::expr right = eachExprToZ3(se->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Sle\n");
			try {
				if (left.is_bv()) {
					res = z3::to_expr(z3_ctx, Z3_mk_bvsle(z3_ctx, left, right));
				} else {
					res = z3::to_expr(z3_ctx, Z3_mk_le(z3_ctx, left, right));
				}
			} catch (z3::exception &ex) {
				std::cerr << "Sle exception : " << ex << std::endl;
			}
		return res;
	}

		case Expr::Ne: {
			NeExpr *ne = cast<NeExpr>(ele);
			if (ne->left.get()->isFloat || ne->right.get()->isFloat) {
				ne->left.get()->isFloat = true;
				ne->right.get()->isFloat = true;
			}
			z3::expr left = eachExprToZ3(ne->left);
			z3::expr right = eachExprToZ3(ne->right);

//			assert(
//					left.get_sort() == right.get_sort()
//							&& "sort between left and right are different in Expr::Ne\n");

			res = (left != right);
		return res;
	}

		//stp unhandled type
		/*	  case Expr::Ne:
		 case Expr::Ugt:
		 case Expr::Uge:
		 case Expr::Sgt:
		 case Expr::Sge:
		 */
	default:
		assert(0 && "unhandled Expr type in kueryExpr to Z3Expr");
		return res;

	}
	std::cerr << "end of switch\n";
	return res;
}

void KQuery2Z3::printZ3AndKqueryExpr() {
	int size = vecZ3Expr.size();

	for (int i = 0; i < size; i++) {
		kqueryExpr[i].get()->dump();
		std::cerr << "--------------------\n";
		std::cerr << vecZ3Expr[i] << std::endl;
		std::cerr << "++++++++++++++++++++\n";
	}
}

void KQuery2Z3::printKquery() {
	int size = vecZ3Expr.size();
	std::cerr << "vec size of vecZ3Expr = " << size << std::endl;
	for (int i = 0; i < size; i++) {
		kqueryExpr[i]->dump();
	}
}

z3::expr KQuery2Z3::getZ3Expr(ref<Expr>& e) {
	z3::expr res = eachExprToZ3(e);
	return res;
}
