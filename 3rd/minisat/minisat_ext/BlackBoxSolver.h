#ifndef SamplingCA_BlackBox_Solver_h
#define SamplingCA_BlackBox_Solver_h

#include <vector>

namespace CDCLSolver {
using std::vector;

class Solver {
public:
    Solver();
    ~Solver();
    void read_clauses(int nvar, const vector<vector<int> >& clauses);
    void add_assumption(int var, int truth_value);
    void add_assumption(int lit);
    void clear_assumptions();
    bool solve();
    void get_solution(vector<int>& tc);
    void set_polarity(int vid, bool bit);
    void add_clause(const vector<int>& cls1, const vector<bool>& cls2);
    void remove_clause_last();
    void add_var(int nvar);

protected:
    void* internal_solver;
    vector<int> assumptions;
};

}

#endif