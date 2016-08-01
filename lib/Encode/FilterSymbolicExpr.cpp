#ifndef LIB_CORE_FILTERSYMBOLICEXPR_C_
#define LIB_CORE_FILTERSYMBOLICEXPR_C_

#include "FilterSymbolicExpr.h"

#include <llvm/IR/Constant.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/Casting.h>
#include <cassert>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>

#include "../../include/klee/Internal/Module/KInstruction.h"
#include "Event.h"

#define OPTIMIZATION1 1

namespace klee {

	std::string FilterSymbolicExpr::getName(ref<klee::Expr> value) {
		std::string globalName = getGlobalName(value);
		std::string name = getName(globalName);
		return name;
	}

	std::string FilterSymbolicExpr::getName(std::string globalName) {
		std::stringstream name;
		name.str("");
		unsigned int i = 0;
		while ((globalName.at(i) != 'S') && (globalName.at(i) != 'L')) {
			name << globalName.at(i);
			i++;
		}
		return name.str();
	}

	std::string FilterSymbolicExpr::getGlobalName(ref<klee::Expr> value) {

		ReadExpr *revalue;
		if (value->getKind() == Expr::Concat) {
			ConcatExpr *ccvalue = cast<ConcatExpr>(value);
			revalue = cast<ReadExpr>(ccvalue->getKid(0));
		} else if (value->getKind() == Expr::Read) {
			revalue = cast<ReadExpr>(value);
		} else {
			assert(0 && "getGlobalName");
		}
		std::string globalName = revalue->updates.root->name;
		return globalName;
	}

	void FilterSymbolicExpr::resolveSymbolicExpr(ref<klee::Expr> symbolicExpr, std::set<std::string> &relatedSymbolicExpr) {
		if (symbolicExpr->getKind() == Expr::Read) {
			std::string name = getName(symbolicExpr);
			if (relatedSymbolicExpr.find(name) == relatedSymbolicExpr.end()) {
				relatedSymbolicExpr.insert(name);
			}
			return;
		} else {
			unsigned kidsNum = symbolicExpr->getNumKids();
			if (kidsNum == 2 && symbolicExpr->getKid(0) == symbolicExpr->getKid(1)) {
				resolveSymbolicExpr(symbolicExpr->getKid(0), relatedSymbolicExpr);
			} else {
				for (unsigned int i = 0; i < kidsNum; i++) {
					resolveSymbolicExpr(symbolicExpr->getKid(i), relatedSymbolicExpr);
				}
			}
		}
	}

	void FilterSymbolicExpr::resolveTaintExpr(ref<klee::Expr> taintExpr, std::vector<ref<klee::Expr> > &relatedTaintExpr, bool &isTaint) {
		if (taintExpr->getKind() == Expr::Concat || taintExpr->getKind() == Expr::Read) {
			unsigned i;
			for (i = 0; i < relatedTaintExpr.size(); i++) {
				if (relatedTaintExpr[i] == taintExpr) {
					break;
				}
			}
			if (i == relatedTaintExpr.size()) {
				relatedTaintExpr.push_back(taintExpr);
				if (taintExpr->isTaint) {
					isTaint = true;
				}
			}
			return;
		} else if (taintExpr->getKind() == Expr::Constant) {
			if (taintExpr->isTaint) {
				isTaint = true;
			}
		} else {
			unsigned kidsNum = taintExpr->getNumKids();
			if (kidsNum == 2 && taintExpr->getKid(0) == taintExpr->getKid(1)) {
				resolveTaintExpr(taintExpr->getKid(0), relatedTaintExpr, isTaint);
			} else {
				for (unsigned int i = 0; i < kidsNum; i++) {
					resolveTaintExpr(taintExpr->getKid(i), relatedTaintExpr, isTaint);
				}
			}
		}
	}

	void FilterSymbolicExpr::addExprToSet(std::set<std::string> *Expr, std::set<std::string> *relatedSymbolicExpr) {

		for (std::set<std::string>::iterator it = Expr->begin(), ie = Expr->end(); it != ie; ++it) {
			std::string name = *it;
			if (relatedSymbolicExpr->find(name) == relatedSymbolicExpr->end()) {
				relatedSymbolicExpr->insert(name);
			}
		}
	}

	void FilterSymbolicExpr::addExprToVector(std::vector<std::string> *Expr, std::vector<std::string> *relatedSymbolicExpr) {
		for (std::vector<std::string>::iterator it = Expr->begin(), ie = Expr->end(); it != ie; ++it) {
			std::string nvarName = *it;
			bool isFind = false;
			for (std::vector<std::string>::iterator itt = relatedSymbolicExpr->begin(), iee = relatedSymbolicExpr->end(); itt != iee;
					++itt) {
				if (*itt == nvarName) {
					isFind = true;
				}
			}
			if (!isFind) {
				relatedSymbolicExpr->push_back(nvarName);
			}
		}
	}

	void FilterSymbolicExpr::addExprToVector(std::set<std::string> *Expr, std::vector<std::string> *relatedSymbolicExpr) {

		for (std::set<std::string>::iterator it = Expr->begin(), ie = Expr->end(); it != ie; ++it) {
			std::string varName = *it;
			bool isFind = false;
			for (std::vector<std::string>::iterator itt = relatedSymbolicExpr->begin(), iee = relatedSymbolicExpr->end(); itt != iee;
					++itt) {
				if (*itt == varName) {
					isFind = true;
				}
			}
			if (!isFind) {
				relatedSymbolicExpr->push_back(varName);
			}
		}
	}

	void FilterSymbolicExpr::addExprToRelate(std::set<std::string> *Expr) {

		for (std::set<std::string>::iterator it = Expr->begin(), ie = Expr->end(); it != ie; ++it) {
			std::string name = *it;
			if (allRelatedSymbolicExprSet.find(name) == allRelatedSymbolicExprSet.end()) {
				allRelatedSymbolicExprSet.insert(name);
				allRelatedSymbolicExprVector.push_back(name);
			}
		}
	}

	bool FilterSymbolicExpr::isRelated(std::string varName) {
		if (allRelatedSymbolicExprSet.find(varName) != allRelatedSymbolicExprSet.end()) {
			return true;
		} else {
			return false;
		}
	}

	void FilterSymbolicExpr::fillterTrace(Trace* trace, std::set<std::string> RelatedSymbolicExpr) {
		std::string name;

		std::vector<ref<klee::Expr> > &pathCondition = trace->pathCondition;
		std::vector<ref<klee::Expr> > &pathConditionRelatedToBranch = trace->pathConditionRelatedToBranch;
		pathConditionRelatedToBranch.clear();
		for (std::vector<ref<Expr> >::iterator it = pathCondition.begin(), ie = pathCondition.end(); it != ie; ++it) {
			name = getName(it->get()->getKid(1));
			if (RelatedSymbolicExpr.find(name) != RelatedSymbolicExpr.end() || !OPTIMIZATION1) {
				pathConditionRelatedToBranch.push_back(*it);
			}
		}

		std::map<std::string, std::vector<Event *> > &readSet = trace->readSet;
		std::map<std::string, std::vector<Event *> > &readSetRelatedToBranch = trace->readSetRelatedToBranch;
		readSetRelatedToBranch.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = readSet.begin(), nie = readSet.end(); nit != nie; ++nit) {
			name = nit->first;
			if (RelatedSymbolicExpr.find(name) != RelatedSymbolicExpr.end() || !OPTIMIZATION1) {
				readSetRelatedToBranch.insert(*nit);
			}
		}

		std::map<std::string, std::vector<Event *> > &writeSet = trace->writeSet;
		std::map<std::string, std::vector<Event *> > &writeSetRelatedToBranch = trace->writeSetRelatedToBranch;
		writeSetRelatedToBranch.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = writeSet.begin(), nie = writeSet.end(); nit != nie; ++nit) {
			name = nit->first;
			if (RelatedSymbolicExpr.find(name) != RelatedSymbolicExpr.end() || !OPTIMIZATION1) {
				writeSetRelatedToBranch.insert(*nit);
			}
		}

		std::map<std::string, llvm::Constant*> &global_variable_initializer = trace->global_variable_initializer;
		std::map<std::string, llvm::Constant*> &global_variable_initisalizer_RelatedToBranch =
				trace->global_variable_initializer_RelatedToBranch;
		global_variable_initisalizer_RelatedToBranch.clear();
		for (std::map<std::string, llvm::Constant*>::iterator nit = global_variable_initializer.begin(), nie =
				global_variable_initializer.end(); nit != nie; ++nit) {
			name = nit->first;
			if (RelatedSymbolicExpr.find(name) != RelatedSymbolicExpr.end() || !OPTIMIZATION1) {
				global_variable_initisalizer_RelatedToBranch.insert(*nit);
			}
		}

		for (std::vector<Event*>::iterator currentEvent = trace->path.begin(), endEvent = trace->path.end(); currentEvent != endEvent;
				currentEvent++) {
			if ((*currentEvent)->isGlobal == true) {
				if ((*currentEvent)->inst->inst->getOpcode() == llvm::Instruction::Load
						|| (*currentEvent)->inst->inst->getOpcode() == llvm::Instruction::Store) {
					if (RelatedSymbolicExpr.find((*currentEvent)->name) == RelatedSymbolicExpr.end() && OPTIMIZATION1) {
						(*currentEvent)->isEventRelatedToBranch = false;
					} else {
						(*currentEvent)->isEventRelatedToBranch = true;
					}
				}
			}
		}

	}

	void FilterSymbolicExpr::filterUseless(Trace* trace) {

		std::string name;
		std::vector<std::string> remainingExprName;
		std::vector<ref<klee::Expr> > remainingExpr;
		allRelatedSymbolicExprSet.clear();
		allRelatedSymbolicExprVector.clear();
		remainingExprName.clear();
		remainingExpr.clear();
		std::vector<ref<klee::Expr> > &storeSymbolicExpr = trace->storeSymbolicExpr;
		for (std::vector<ref<Expr> >::iterator it = storeSymbolicExpr.begin(), ie = storeSymbolicExpr.end(); it != ie; ++it) {
			name = getName(it->get()->getKid(1));
			remainingExprName.push_back(name);
			remainingExpr.push_back(it->get());
		}

		std::vector<ref<klee::Expr> > &brSymbolicExpr = trace->brSymbolicExpr;
		std::vector<std::set<std::string>*> &brRelatedSymbolicExpr = trace->brRelatedSymbolicExpr;
		for (std::vector<ref<Expr> >::iterator it = brSymbolicExpr.begin(), ie = brSymbolicExpr.end(); it != ie; ++it) {
			std::set<std::string> *tempSymbolicExpr = new std::set<std::string>;
			resolveSymbolicExpr(it->get(), *tempSymbolicExpr);
			brRelatedSymbolicExpr.push_back(tempSymbolicExpr);
			addExprToRelate(tempSymbolicExpr);
		}

		std::vector<ref<klee::Expr> > &assertSymbolicExpr = trace->assertSymbolicExpr;
		std::vector<std::set<std::string>*> &assertRelatedSymbolicExpr = trace->assertRelatedSymbolicExpr;
		for (std::vector<ref<Expr> >::iterator it = assertSymbolicExpr.begin(), ie = assertSymbolicExpr.end(); it != ie; ++it) {
			std::set<std::string> *tempSymbolicExpr = new std::set<std::string>;
			resolveSymbolicExpr(it->get(), *tempSymbolicExpr);
			assertRelatedSymbolicExpr.push_back(tempSymbolicExpr);
			addExprToRelate(tempSymbolicExpr);
		}

		std::vector<ref<klee::Expr> > &pathCondition = trace->pathCondition;
		std::map<std::string, std::set<std::string>*> &varRelatedSymbolicExpr = trace->allRelatedSymbolicExpr;
		for (std::vector<std::string>::iterator nit = allRelatedSymbolicExprVector.begin(); nit != allRelatedSymbolicExprVector.end();
				++nit) {
			name = *nit;
#if FILTER_DEBUG
			llvm::errs() << "allRelatedSymbolicExprSet name : " << name << "\n";
#endif
			std::vector<ref<Expr> >::iterator itt = remainingExpr.begin();
			for (std::vector<std::string>::iterator it = remainingExprName.begin(), ie = remainingExprName.end(); it != ie;) {
				if (name == *it) {
					remainingExprName.erase(it);
					--ie;
					pathCondition.push_back(*itt);
#if FILTER_DEBUG
					llvm::errs() << *itt << "\n";
#endif
					std::set<std::string> *tempSymbolicExpr = new std::set<std::string>;
					resolveSymbolicExpr(*itt, *tempSymbolicExpr);
					if (varRelatedSymbolicExpr.find(name) != varRelatedSymbolicExpr.end()) {
						addExprToSet(tempSymbolicExpr, varRelatedSymbolicExpr[name]);
					} else {
						varRelatedSymbolicExpr[name] = tempSymbolicExpr;
					}
					addExprToRelate(tempSymbolicExpr);
					remainingExpr.erase(itt);
				} else {
					++it;
					++itt;
				}
			}
		}

#if FILTER_DEBUG
		llvm::errs() << "allRelatedSymbolicExprSet" << "\n";
		for (std::set<std::string>::iterator nit = allRelatedSymbolicExprSet.begin(); nit != allRelatedSymbolicExprSet.end(); ++nit) {
			llvm::errs() << (*nit).c_str() << "\n";
		}
#endif

		std::map<std::string, long> &varThread = trace->varThread;

		std::map<std::string, std::vector<Event *> > usefulReadSet;
		std::map<std::string, std::vector<Event *> > &readSet = trace->readSet;
		std::map<std::string, std::vector<Event *> > &allReadSet = trace->allReadSet;
		usefulReadSet.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = readSet.begin(), nie = readSet.end(); nit != nie; ++nit) {
			allReadSet.insert(*nit);
			name = nit->first;
			if (isRelated(name) || !OPTIMIZATION1) {
				usefulReadSet.insert(*nit);
				if (varThread.find(name) == varThread.end()) {
					varThread[name] = (*(nit->second.begin()))->threadId;
				}
				long id = varThread[name];
				if (id != 0) {
					for (std::vector<Event *>::iterator it = nit->second.begin(), ie = nit->second.end(); it != ie; ++it) {
						if (id != (*it)->threadId) {
							varThread[name] = 0;
							break;
						}
					}
				}
			}
		}
		readSet.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = usefulReadSet.begin(), nie = usefulReadSet.end(); nit != nie;
				++nit) {
			readSet.insert(*nit);
		}

		std::map<std::string, std::vector<Event *> > usefulWriteSet;
		std::map<std::string, std::vector<Event *> > &writeSet = trace->writeSet;
		std::map<std::string, std::vector<Event *> > &allWriteSet = trace->allWriteSet;
		usefulWriteSet.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = writeSet.begin(), nie = writeSet.end(); nit != nie; ++nit) {
			allWriteSet.insert(*nit);
			name = nit->first;
			if (isRelated(name) || !OPTIMIZATION1) {
				usefulWriteSet.insert(*nit);
				if (varThread.find(name) == varThread.end()) {
					varThread[name] = (*(nit->second.begin()))->threadId;
				}
				long id = varThread[name];
				if (id != 0) {
					for (std::vector<Event *>::iterator it = nit->second.begin(), ie = nit->second.end(); it != ie; ++it) {
						if (id != (*it)->threadId) {
							varThread[name] = 0;
							break;
						}
					}
				}
			}
		}
		writeSet.clear();
		for (std::map<std::string, std::vector<Event *> >::iterator nit = usefulWriteSet.begin(), nie = usefulWriteSet.end(); nit != nie;
				++nit) {
			writeSet.insert(*nit);
		}

		for (std::map<std::string, long>::iterator nit = varThread.begin(), nie = varThread.end(); nit != nie; ++nit) {
			if (usefulWriteSet.find((*nit).first) == usefulWriteSet.end()) {
				(*nit).second = -1;
			}
		}

		std::map<std::string, llvm::Constant*> usefulGlobal_variable_initializer;
		std::map<std::string, llvm::Constant*> &global_variable_initializer = trace->global_variable_initializer;
#if FILTER_DEBUG
		llvm::errs() << "global_variable_initializer = " << trace->global_variable_initializer.size() << "\n";
		for (std::map<std::string, llvm::Constant*>::iterator it = trace->global_variable_initializer.begin(), ie =
				trace->global_variable_initializer.end(); it != ie; ++it) {
			llvm::errs() << it->first << "\n";
		}
#endif
		usefulGlobal_variable_initializer.clear();
		for (std::map<std::string, llvm::Constant*>::iterator nit = global_variable_initializer.begin(), nie =
				global_variable_initializer.end(); nit != nie; ++nit) {
			name = nit->first;
			if (isRelated(name) || !OPTIMIZATION1) {
				usefulGlobal_variable_initializer.insert(*nit);
			}
		}
		global_variable_initializer.clear();
		for (std::map<std::string, llvm::Constant*>::iterator nit = usefulGlobal_variable_initializer.begin(), nie =
				usefulGlobal_variable_initializer.end(); nit != nie; ++nit) {
			global_variable_initializer.insert(*nit);
		}
#if FILTER_DEBUG
		llvm::errs() << "global_variable_initializer = " << trace->global_variable_initializer.size() << "\n";
		for (std::map<std::string, llvm::Constant*>::iterator it = trace->global_variable_initializer.begin(), ie =
				trace->global_variable_initializer.end(); it != ie; ++it) {
			llvm::errs() << it->first << "\n";
		}
#endif
		for (std::vector<Event*>::iterator currentEvent = trace->path.begin(), endEvent = trace->path.end(); currentEvent != endEvent;
				currentEvent++) {
			if ((*currentEvent)->isGlobal == true) {
				if ((*currentEvent)->inst->inst->getOpcode() == llvm::Instruction::Load
						|| (*currentEvent)->inst->inst->getOpcode() == llvm::Instruction::Store) {
					if (isRelated((*currentEvent)->name) && OPTIMIZATION1) {
						(*currentEvent)->isEventRelatedToBranch = false;
					} else {
						(*currentEvent)->isEventRelatedToBranch = true;
					}
				} else {
					(*currentEvent)->isEventRelatedToBranch = true;
				}
			}
		}

		std::vector<ref<klee::Expr> > rwSymbolicExprRelatedToBranch;
		std::vector<ref<klee::Expr> > &rwSymbolicExpr = trace->rwSymbolicExpr;
		for (std::vector<ref<klee::Expr> >::iterator nit = rwSymbolicExpr.begin(), nie = rwSymbolicExpr.end(); nit != nie; ++nit) {
			name = getName((*nit)->getKid(1));
			if (isRelated(name) || !OPTIMIZATION1) {
				rwSymbolicExprRelatedToBranch.push_back(*nit);
			}
		}
		rwSymbolicExpr.clear();
		for (std::vector<ref<klee::Expr> >::iterator nit = rwSymbolicExprRelatedToBranch.begin(), nie = rwSymbolicExprRelatedToBranch.end();
				nit != nie; ++nit) {
			rwSymbolicExpr.push_back(*nit);
		}

		fillterTrace(trace, allRelatedSymbolicExprSet);

	}

	bool FilterSymbolicExpr::filterUselessWithSet(Trace* trace, std::set<std::string>* relatedSymbolicExpr) {
		std::set<std::string> &RelatedSymbolicExpr = trace->RelatedSymbolicExpr;
		RelatedSymbolicExpr.clear();
		addExprToSet(relatedSymbolicExpr, &RelatedSymbolicExpr);

		std::string name;
		std::map<std::string, std::set<std::string>*> &allRelatedSymbolicExpr = trace->allRelatedSymbolicExpr;
		for (std::set<std::string>::iterator nit = RelatedSymbolicExpr.begin(); nit != RelatedSymbolicExpr.end(); ++nit) {
			name = *nit;
			if (allRelatedSymbolicExpr.find(name) != allRelatedSymbolicExpr.end()) {
				addExprToSet(allRelatedSymbolicExpr[name], &RelatedSymbolicExpr);
			}
		}

		bool branch = false;
		std::map<std::string, long> &varThread = trace->varThread;
		for (std::set<std::string>::iterator it = RelatedSymbolicExpr.begin(), ie = RelatedSymbolicExpr.end(); it != ie; ++it) {
			if (varThread[*it] == 0) {
				branch = true;
				break;
			}
		}
		if (branch) {
//		fillterTrace(trace, RelatedSymbolicExpr);
			return true;
		} else {
			return false;
		}
	}

	void FilterSymbolicExpr::filterUselessByTaint(Trace* trace) {

		std::set<std::string> &taintSymbolicExpr = trace->taintSymbolicExpr;
		std::vector<std::string> taint;
		for (std::set<std::string>::iterator it = taintSymbolicExpr.begin(), ie = taintSymbolicExpr.end(); it != ie; it++) {
			taint.push_back(*it);
		}

		std::set<std::string> &unTaintSymbolicExpr = trace->unTaintSymbolicExpr;
		std::vector<std::string> unTaint;
		for (std::set<std::string>::iterator it = unTaintSymbolicExpr.begin(), ie = unTaintSymbolicExpr.end(); it != ie; it++) {
			unTaint.push_back(*it);
		}

		std::string name;
		std::map<std::string, std::set<std::string>*> &allRelatedSymbolicExpr = trace->allRelatedSymbolicExpr;
		for (std::vector<std::string>::iterator it = taint.begin(); it != taint.end(); it++) {
			name = *it;
			for (std::vector<std::string>::iterator itt = unTaint.begin(); itt != unTaint.end();) {
				if (allRelatedSymbolicExpr.find(*itt) == allRelatedSymbolicExpr.end()) {
					itt++;
				} else if (allRelatedSymbolicExpr[*itt]->find(name) != allRelatedSymbolicExpr[*itt]->end()) {
					taint.push_back(*itt);
					unTaint.erase(itt);
				} else {
					itt++;
				}
			}
		}

		std::set<std::string> &potentialTaintSymbolicExpr = trace->potentialTaint;
		for (std::vector<std::string>::iterator it = taint.begin(), ie = taint.end(); it != ie; it++) {
			name = *it;
			if (taintSymbolicExpr.find(name) == taintSymbolicExpr.end()) {
				potentialTaintSymbolicExpr.insert(name);
			}
		}
	}

}

#endif /* LIB_CORE_FILTERSYMBOLICEXPR_C_ */
