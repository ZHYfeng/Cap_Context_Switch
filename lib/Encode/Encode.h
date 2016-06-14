/*
 * Encode.h
 *
 *  Created on: Jun 10, 2014
 *      Author: xdzhang
 */

#ifndef ENCODE_H_
#define ENCODE_H_


#include <z3++.h>
#include <stack>
#include <utility>

#include "Trace.h"
#include "KQuery2Z3.h"
#include "Event.h"
#include "FilterSymbolicExpr.h"
#include "RuntimeDataManager.h"
enum InstType {
	NormalOp, GlobalVarOp, ThreadOp
};
using namespace llvm;
using namespace z3;
using namespace std;
namespace klee {

class Encode {
private:
	RuntimeDataManager* runtimeData;
	Trace* trace; //all data about encoding
	context z3_ctx;
	solver z3_solver;
	solver z3_taint_solver;
	FilterSymbolicExpr filter;
	unsigned formulaNum;
	unsigned solvingTimes;

public:
	Encode(RuntimeDataManager* data) :
			runtimeData(data), z3_solver(z3_ctx), z3_taint_solver(z3_ctx) {
		trace = data->getCurrentTrace();
		formulaNum = 0;
		solvingTimes = 0;
	}
	~Encode() {
		runtimeData->allFormulaNum += formulaNum;
		runtimeData->solvingTimes += solvingTimes;
	}
	void buildAllFormula();
	void buildifAndassert();
	void buildPTSFormula();
	void showInitTrace();
	void check_output();
	void check_if();
	bool verify();

	void PTS();

private:

	////////////////////////////////modify this sagment at the end/////////////////////////
	vector<pair<expr, expr> > globalOutputFormula;
	//first--equality, second--constrait which equality must satisfy.

	vector<pair<Event*, expr> > ifFormula;
	vector<pair<Event*, expr> > assertFormula;
	vector<pair<Event*, expr> > rwFormula;

	void buildInitValueFormula(solver z3_solver_init);
	void buildPathCondition(solver z3_solver_pc);
	void buildMemoryModelFormula(solver z3_solver_mm);
	void buildPartialOrderFormula(solver z3_solver_po);
	void buildReadWriteFormula(solver z3_solver_rw);
	void buildSynchronizeFormula(solver z3_solver_sync);
	void buildInitTaintFormula(solver z3_solver_it);
	void buildTaintMatchFormula(solver z3_solver_tm);
	void buildTaintProgatationFormula(solver z3_solver_tp);
	void buildOutputFormula();

	expr buildExprForConstantValue(Value *V, bool isLeft, string prefix);

private:
	void markLatestWriteForGlobalVar();

	map<string, Event*> latestWriteOneThread;
	map<int, map<string, Event*> > allThreadLastWrite;
	map<string, expr> eventNameInZ3; //int:eventid.add data in function buildMemoryModelFormula
	z3::sort llvmTy_to_z3Ty(const Type *typ);

	//key--local var, value--index..like ssa
	expr makeExprsAnd(vector<expr> exprs);
	expr makeExprsOr(vector<expr> exprs);
	expr makeExprsSum(vector<expr> exprs);
	expr enumerateOrder(Event *read, Event *write, Event *anotherWrite);
	expr readFromWriteFormula(Event *read, Event *write, string var);
	bool readFromInitFormula(Event * read, expr& ret);

	expr taintReadFromWriteFormula(Event *read, Event *write, string var);
	bool taintReadFromInitFormula(Event * read, expr& ret);

	void computePrefix(vector<Event*>& vecEvent, Event* ifEvent);
	void showPrefixInfo(Prefix* prefix, Event* ifEvent);

	void printSourceLine(string path, vector<struct Pair *>& eventIndexPair);
	void printSourceLine(string path, vector<Event *>& trace);
	string readLine(string filename, unsigned line);
	bool isAssert(string filename, unsigned line);
	string stringTrim(char * source);
	InstType getInstOpType(Event *event);
	void logStatisticInfo();

	void controlGranularity(int level);

	expr makeOrTaint(ref<klee::Expr> value);

};

}

#endif /* ENCODE_H_ */
