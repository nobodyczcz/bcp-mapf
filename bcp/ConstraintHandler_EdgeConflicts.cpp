/*
This file is part of BCP-MAPF.

BCP-MAPF is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

BCP-MAPF is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with BCP-MAPF.  If not, see <https://www.gnu.org/licenses/>.

Author: Edward Lam <ed@ed-lam.com>
*/

//#define PRINT_DEBUG

#include "ConstraintHandler_EdgeConflicts.h"
#include "ProblemData.h"
#include "VariableData.h"

#define CONSHDLR_NAME                                 "edge_conflicts"
#define CONSHDLR_DESC          "Constraint handler for edge conflicts"
#define CONSHDLR_SEPAPRIORITY                                  +400000 // priority of the constraint handler for separation
#define CONSHDLR_ENFOPRIORITY                                 -1000000 // priority of the constraint handler for constraint enforcing
#define CONSHDLR_CHECKPRIORITY                                -1000000 // priority of the constraint handler for checking feasibility
#define CONSHDLR_SEPAFREQ                                            1 // frequency for separating cuts; zero means to separate only in the root node
#define CONSHDLR_EAGERFREQ                                           1 // frequency for using all instead of only the useful constraints in separation,
                                                                       // propagation and enforcement, -1 for no eager evaluations, 0 for first only
#define CONSHDLR_DELAYSEPA                                       FALSE // should separation method be delayed, if other separators found cuts?
#define CONSHDLR_NEEDSCONS                                        TRUE // should the constraint handler be skipped, if no constraints are available?

// Data for edge conflicts
struct EdgeConflictsConsData
{
    Vector<EdgeConflict> conflicts;
};

// Create a constraint for edge conflicts and include it
SCIP_RETCODE SCIPcreateConsEdgeConflicts(
    SCIP* scip,                 // SCIP
    SCIP_CONS** cons,           // Pointer to hold the created constraint
    const char* name,           // Name of constraint
    SCIP_Bool initial,          // Should the LP relaxation of constraint be in the initial LP?
    SCIP_Bool separate,         // Should the constraint be separated during LP processing?
    SCIP_Bool enforce,          // Should the constraint be enforced during node processing?
    SCIP_Bool check,            // Should the constraint be checked for feasibility?
    SCIP_Bool propagate,        // Should the constraint be propagated during node processing?
    SCIP_Bool local,            // Is constraint only valid locally?
    SCIP_Bool modifiable,       // Is constraint modifiable (subject to column generation)?
    SCIP_Bool dynamic,          // Is constraint subject to aging?
    SCIP_Bool removable,        // Should the relaxation be removed from the LP due to aging or cleanup?
    SCIP_Bool stickingatnode    // Should the constraint always be kept at the node where it was added, even
                                // if it may be moved to a more global node?
)
{
    // Find constraint handler.
    SCIP_CONSHDLR* conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
    release_assert(conshdlr, "Constraint handler for edge conflicts is not found");

    // Create constraint data.
    EdgeConflictsConsData* consdata = nullptr;
    SCIP_CALL(SCIPallocBlockMemory(scip, &consdata));
    debug_assert(consdata);
    new(consdata) EdgeConflictsConsData;
    consdata->conflicts.reserve(5000);

    // Create constraint.
    SCIP_CALL(SCIPcreateCons(scip,
                             cons,
                             name,
                             conshdlr,
                             reinterpret_cast<SCIP_CONSDATA*>(consdata),
                             initial,
                             separate,
                             enforce,
                             check,
                             propagate,
                             local,
                             modifiable,
                             dynamic,
                             removable,
                             stickingatnode));

    // Done.
    return SCIP_OKAY;
}

SCIP_RETCODE edge_conflicts_create_cut(
    SCIP* scip,                         // SCIP
    SCIP_CONS* cons,                    // Constraint
    EdgeConflictsConsData* consdata,    // Constraint data
    const Time t,                       // Time
#ifdef USE_WAITEDGE_CONFLICTS
    Array<Edge, 3> edges,               // Edges in the conflict
#else
    Array<Edge, 2> edges,               // Edges in the conflict
#endif
    const Vector<SCIP_VAR*>& vars,      // Array of variables
    SCIP_Result* result                 // Output result
)
{
    // Create constraint name.
#ifdef DEBUG
    auto probdata = SCIPgetProbData(scip);
    const auto& map = SCIPprobdataGetMap(probdata);
    const auto [x1, y1] = map.get_xy(edges[0].n);
    const auto [x2, y2] = map.get_destination_xy(edges[0]);
    const auto name = fmt::format("edge_conflict(({},{}),({},{}),{})", x1, y1, x2, y2, t);
#endif

    // Create a row.
    SCIP_ROW* row = nullptr;
    SCIP_CALL(SCIPcreateEmptyRowCons(scip,
                                     &row,
                                     cons,
#ifdef DEBUG
                                     name.c_str(),
#else
                                     "",
#endif
                                     -SCIPinfinity(scip),
                                     1.0,
                                     FALSE,
                                     TRUE,
                                     FALSE));
    debug_assert(row);

    // Add variables to the constraint.
    SCIP_CALL(SCIPcacheRowExtensions(scip, row));
#ifdef DEBUG
    SCIP_Real lhs = 0.0;
#endif
    for (auto var : vars)
    {
        // Get the path.
        debug_assert(var);
        auto vardata = SCIPvarGetData(var);
        const auto path_length = SCIPvardataGetPathLength(vardata);
        const auto path = SCIPvardataGetPath(vardata);

        // Add coefficients.
#ifdef USE_WAITEDGE_CONFLICTS
        if ((t < path_length - 1 && (path[t] == edges[0] || path[t] == edges[1] || path[t] == edges[2])) ||
            (t >= path_length - 1 && path[path_length - 1].n == edges[2].n))
#else
        if (t < path_length - 1 && (path[t] == edges[0] || path[t] == edges[1]))
#endif
        {
            // Print.
            debugln("      Agent: {:2d}, Val: {:7.4f}, Path: {}",
                    SCIPvardataGetAgent(vardata),
                    SCIPgetSolVal(scip, nullptr, var),
                    format_path_spaced(SCIPgetProbData(scip), path_length, path));

            // Add the coefficient.
            SCIP_CALL(SCIPaddVarToRow(scip, row, var, 1));
#ifdef DEBUG
            lhs += SCIPgetSolVal(scip, nullptr, var);
#endif
        }
    }
    SCIP_CALL(SCIPflushRowExtensions(scip, row));
#ifdef DEBUG
    debug_assert(SCIPisGT(scip, lhs, 1.0));
#endif

    // Add the row to the LP.
    SCIP_Bool infeasible;
    SCIP_CALL(SCIPaddRow(scip, row, TRUE, &infeasible));

    // Set status.
    if (infeasible)
    {
        *result = SCIP_CUTOFF;
    }
    else
    {
        *result = SCIP_SEPARATED;
    }

    // Store the constraint.
    consdata->conflicts.push_back({row, edges, t});

    // Done.
    return SCIP_OKAY;
}

// Checker
static
SCIP_RETCODE edge_conflicts_check(
    SCIP* scip,                 // SCIP
    SCIP_SOL* sol,              // Solution
    SCIP_RESULT* result         // Pointer to store the result
)
{
    // Print.
    debugln("Starting checker for edge conflicts on solution with obj {:.6f}:",
            SCIPgetSolOrigObj(scip, sol));

    // Get problem data.
    auto probdata = SCIPgetProbData(scip);
    const auto& map = SCIPprobdataGetMap(probdata);

    // Get variables.
    const auto& vars = SCIPprobdataGetVars(probdata);

    // Calculate the number of times an edge is used by summing the columns.
    HashTable<EdgeTime, SCIP_Real> edge_times_used;
    for (auto var : vars)
    {
        // Get the path.
        debug_assert(var);
        auto vardata = SCIPvarGetData(var);
        const auto path_length = SCIPvardataGetPathLength(vardata);
        const auto path = SCIPvardataGetPath(vardata);

        // Get the variable value.
        const auto var_val = SCIPgetSolVal(scip, sol, var);

        // Sum.
        if (SCIPisPositive(scip, var_val))
        {
            // Wait action cannot be in a conflict.
            for (Time t = 0; t < path_length - 1; ++t)
                if (path[t].d != Direction::WAIT)
                {
                    const auto e = map.get_undirected_edge(path[t]);
                    const EdgeTime et(e, t);
                    edge_times_used[et] += var_val;
                }
        }
    }

    // Check for conflicts.
    for (const auto [et, val] : edge_times_used)
        if (SCIPisGT(scip, val, 1.0))
        {
            // Print.
#ifdef PRINT_DEBUG
            {
                const auto [x1, y1] = map.get_xy(et.n);
                const auto [x2, y2] = map.get_destination_xy(et.et.e);

                debugln("   Infeasible solution has edge (({},{}),({},{})) (node ID {}, "
                        "direction {}) at time {} with value {}",
                        x1, y1, x2, y2, et.n, et.d, et.t, val);
            }
#endif

            // Infeasible.
            *result = SCIP_INFEASIBLE;
            return SCIP_OKAY;
        }

    // Done.
    return SCIP_OKAY;
}

// Separator
static
SCIP_RETCODE edge_conflicts_separate(
    SCIP* scip,                 // SCIP
    SCIP_CONS* cons,            // Constraint
    SCIP_SOL* sol,              // Solution
    SCIP_RESULT* result         // Pointer to store the result
)
{
    // Print.
    debugln("Starting separator for edge conflicts on solution with obj {:.6f}:",
            SCIPgetSolOrigObj(scip, sol));

    // Print paths.
#ifdef PRINT_DEBUG
    print_used_paths(scip);
#endif

    // Get constraint data.
    auto consdata = reinterpret_cast<EdgeConflictsConsData*>(SCIPconsGetData(cons));
    debug_assert(consdata);

    // Get problem data.
    auto probdata = SCIPgetProbData(scip);
    const auto& map = SCIPprobdataGetMap(probdata);

    // Get variables.
    const auto& vars = SCIPprobdataGetVars(probdata);

    // Find the makespan.
    Time makespan = 0;
    for (auto var : vars)
    {
        // Get the path length.
        debug_assert(var);
        auto vardata = SCIPvarGetData(var);
        const auto path_length = SCIPvardataGetPathLength(vardata);

        // Get the variable value.
        const auto var_val = SCIPgetSolVal(scip, sol, var);

        // Store the length of the longest path.
        if (path_length > makespan && SCIPisPositive(scip, var_val))
        {
            makespan = path_length;
        }
    }

    // Calculate the number of times an edge is used by summing the columns.
    HashTable<EdgeTime, SCIP_Real> move_edges_used;
#ifdef USE_WAITEDGE_CONFLICTS
    HashTable<EdgeTime, SCIP_Real> wait_edges_used;
#endif
    for (auto var : vars)
    {
        // Get the path.
        debug_assert(var);
        auto vardata = SCIPvarGetData(var);
        const auto path_length = SCIPvardataGetPathLength(vardata);
        const auto path = SCIPvardataGetPath(vardata);

        // Get the variable value.
        const auto var_val = SCIPgetSolVal(scip, sol, var);

        // Sum edge value.
        if (SCIPisPositive(scip, var_val))
        {
            Time t = 0;
            for (; t < path_length - 1; ++t)
            {
                if (path[t].d != Direction::WAIT)
                {
                    const auto e = map.get_undirected_edge(path[t]);
                    const EdgeTime et{e, t};
                    move_edges_used[et] += var_val;
                }
#ifdef USE_WAITEDGE_CONFLICTS
                else
                {
                    const auto e = path[t];
                    debug_assert(e.d == Direction::WAIT);
                    const EdgeTime et{e, t};
                    wait_edges_used[et] += var_val;
                }
#endif
            }

#ifdef USE_WAITEDGE_CONFLICTS
            const auto n = path[path_length - 1].n;
            for (; t < makespan - 1; ++t)
            {
                const EdgeTime et{n, Direction::WAIT, t};
                wait_edges_used[et] += var_val;
            }
#endif
        }
    }

    // Create cuts.
    for (const auto [et, move_val] : move_edges_used)
    {
        // Get the time.
        const auto t = et.t;

        // Get the other direction of the edge.
#ifdef USE_WAITEDGE_CONFLICTS
        Array<Edge, 3> edges;
#else
        Array<Edge, 2> edges;
#endif
        edges[0] = et.et.e;
        edges[1] = map.get_opposite_edge(et.et.e);
        debug_assert(et.et.e.d != Direction::WAIT);

        // Get the wait edge.
#ifdef USE_WAITEDGE_CONFLICTS
        SCIP_Real wait_val;
        {
            // Get the edge weight of the wait edge.
            SCIP_Real wait0_val = 0.0;
            SCIP_Real wait1_val = 0.0;
            if (auto it = wait_edges_used.find(EdgeTime{edges[0].n, Direction::WAIT, t}); it != wait_edges_used.end())
            {
                wait0_val = it->second;
            }
            if (auto it = wait_edges_used.find(EdgeTime{edges[1].n, Direction::WAIT, t}); it != wait_edges_used.end())
            {
                wait1_val = it->second;
            }

            // Choose the wait with higher value.
            if (wait0_val >= wait1_val)
            {
                edges[2] = Edge(edges[0].n, Direction::WAIT);
                wait_val = wait0_val;
            }
            else
            {
                edges[2] = Edge(edges[1].n, Direction::WAIT);
                wait_val = wait1_val;
            }
        }
#else
        constexpr SCIP_Real wait_val = 0.0;
#endif

        // Determine if there is a conflict.
        const auto lhs = move_val + wait_val;
        if (SCIPisGT(scip, lhs, 1.0))
        {
            // Print.
#ifdef PRINT_DEBUG
            {
                const auto [e1_x1, e1_y1] = map.get_xy(edges[0].n);
                const auto [e1_x2, e1_y2] = map.get_destination_xy(edges[0]);

                const auto [e2_x1, e2_y1] = map.get_xy(edges[1].n);
                const auto [e2_x2, e2_y2] = map.get_destination_xy(edges[1]);

                debugln("   Creating edge conflict cut on (({},{}),({},{}),{}) and (({},{}),({},{}),{}) with value {} "
                        "in branch-and-bound node {}",
                        e1_x1, e1_y1, e1_x2, e1_y2, t,
                        e2_x1, e2_y1, e2_x2, e2_y2, t,
                        lhs,
                        SCIPnodeGetNumber(SCIPgetCurrentNode(scip)));
            }
#endif

            // Create cut.
            SCIP_CALL(edge_conflicts_create_cut(scip, cons, consdata, t, edges, vars, result));
        }
    }

    // Done.
    return SCIP_OKAY;
}

// Copy method for constraint handler
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(valid);

    // Include constraint handler.
    SCIP_CALL(SCIPincludeConshdlrEdgeConflicts(scip));

    // Done.
    *valid = TRUE;
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Free constraint data
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSDELETE(consDeleteEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(cons);
    debug_assert(consdata);
    debug_assert(*consdata);

    // Override type.
    auto consdata2 = reinterpret_cast<EdgeConflictsConsData**>(consdata);

    // Free memory.
    (*consdata2)->~EdgeConflictsConsData();
    SCIPfreeBlockMemory(scip, consdata2);

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Free rows
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSEXITSOL(consExitsolEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(nconss == 0 || conss);

    // Loop through all constraints.
    for (Int c = 0; c < nconss; ++c)
    {
        // Get constraint.
        auto cons = conss[c];
        debug_assert(cons);

        // Get constraint data.
        auto consdata = reinterpret_cast<EdgeConflictsConsData*>(SCIPconsGetData(cons));
        debug_assert(consdata);

        // Free row for each edge conflict.
        for (auto& [row, edges, t] : consdata->conflicts)
        {
            SCIP_CALL(SCIPreleaseRow(scip, &row));
        }
        consdata->conflicts.clear();
    }

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Transform constraint data into data belonging to the transformed problem
static
SCIP_DECL_CONSTRANS(consTransEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(sourcecons);
    debug_assert(targetcons);

    // Get data of original constraint.
    auto sourcedata =
        reinterpret_cast<EdgeConflictsConsData*>(SCIPconsGetData(sourcecons));
    debug_assert(sourcedata);

    // Create constraint data.
    EdgeConflictsConsData* targetdata = nullptr;
    SCIP_CALL(SCIPallocBlockMemory(scip, &targetdata));
    debug_assert(targetdata);
    new(targetdata) EdgeConflictsConsData(*sourcedata);

    // Must begin with no edge conflicts.
    release_assert(sourcedata->conflicts.empty(),
                   "Edge conflicts exist in original problem before transformation");

    // Create constraint.
    char name[SCIP_MAXSTRLEN];
    SCIPsnprintf(name, SCIP_MAXSTRLEN, "t_%s", SCIPconsGetName(sourcecons));
    SCIP_CALL(SCIPcreateCons(scip,
                             targetcons,
                             name,
                             conshdlr,
                             reinterpret_cast<SCIP_CONSDATA*>(targetdata),
                             SCIPconsIsInitial(sourcecons),
                             SCIPconsIsSeparated(sourcecons),
                             SCIPconsIsEnforced(sourcecons),
                             SCIPconsIsChecked(sourcecons),
                             SCIPconsIsPropagated(sourcecons),
                             SCIPconsIsLocal(sourcecons),
                             SCIPconsIsModifiable(sourcecons),
                             SCIPconsIsDynamic(sourcecons),
                             SCIPconsIsRemovable(sourcecons),
                             SCIPconsIsStickingAtNode(sourcecons)));

    // Done.
    return SCIP_OKAY;
}

// Feasibility check method for integral solutions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSCHECK(consCheckEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(nconss == 0 || conss);
    debug_assert(result);

    // Start.
    *result = SCIP_FEASIBLE;

    // Start checker.
    debug_assert(sol);
    SCIP_CALL(edge_conflicts_check(scip, sol, result));

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Constraint enforcing method for LP solutions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSENFOLP(consEnfolpEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(conss);
    debug_assert(result);

    // Start.
    *result = SCIP_FEASIBLE;

    // Get constraint.
    debug_assert(nconss == 1);
    auto cons = conss[0];
    debug_assert(cons);

    // Start separator.
    SCIP_CALL(edge_conflicts_separate(scip, cons, nullptr, result));

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Constraint enforcing method for pseudo solutions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSENFOPS(consEnfopsEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(conss);
    debug_assert(result);

    // Start.
    *result = SCIP_FEASIBLE;

    // Start checker.
    SCIP_CALL(edge_conflicts_check(scip, nullptr, result));

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Separation method for LP solutions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSSEPALP(consSepalpEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(conss);
    debug_assert(result);

    // Start.
    *result = SCIP_DIDNOTFIND;

    // Get constraint.
    debug_assert(nconss == 1);
    auto cons = conss[0];
    debug_assert(cons);

    // Start separator.
    SCIP_CALL(edge_conflicts_separate(scip, cons, nullptr, result));

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Separation method for arbitrary primal solutions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSSEPASOL(consSepasolEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(conss);
    debug_assert(result);

    // Start.
    *result = SCIP_DIDNOTFIND;

    // Get constraint.
    debug_assert(nconss == 1);
    auto cons = conss[0];
    debug_assert(cons);

    // Start separator.
    debug_assert(sol);
    SCIP_CALL(edge_conflicts_separate(scip, cons, sol, result));

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Variable rounding lock method of constraint handler
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSLOCK(consLockEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(conshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
    debug_assert(cons);

    // Get problem data.
    auto probdata = SCIPgetProbData(scip);

    // Lock rounding of variables. (Round up may invalidate the constraint.)
    const auto& vars = SCIPprobdataGetVars(probdata);
    for (auto var : vars)
    {
        debug_assert(var);
        SCIP_CALL(SCIPaddVarLocksType(scip, var, locktype, nlocksneg, nlockspos));
    }

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Copying constraint of constraint handler
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static
SCIP_DECL_CONSCOPY(consCopyEdgeConflicts)
{
    // Check.
    debug_assert(scip);
    debug_assert(sourceconshdlr);
    debug_assert(strcmp(SCIPconshdlrGetName(sourceconshdlr), CONSHDLR_NAME) == 0);
    debug_assert(cons);
    debug_assert(sourcescip);
    debug_assert(sourcecons);
    debug_assert(varmap);

    // Stop if invalid.
    if (*valid)
    {
        // Create copied constraint.
        if (!name)
        {
            name = SCIPconsGetName(sourcecons);
        }
        SCIP_CALL(SCIPcreateConsEdgeConflicts(scip,
                                              cons,
                                              name,
                                              initial,
                                              separate,
                                              enforce,
                                              check,
                                              propagate,
                                              local,
                                              modifiable,
                                              dynamic,
                                              removable,
                                              stickingatnode));

        // Mark as valid.
        *valid = TRUE;
    }

    // Done.
    return SCIP_OKAY;
}
#pragma GCC diagnostic pop

// Creates constraint handler for edge conflicts constraints and include it in SCIP
SCIP_RETCODE SCIPincludeConshdlrEdgeConflicts(
    SCIP* scip    // SCIP
)
{
    // Include constraint handler.
    SCIP_CONSHDLR* conshdlr = nullptr;
    SCIP_CALL(SCIPincludeConshdlrBasic(scip,
                                       &conshdlr,
                                       CONSHDLR_NAME,
                                       CONSHDLR_DESC,
                                       CONSHDLR_ENFOPRIORITY,
                                       CONSHDLR_CHECKPRIORITY,
                                       CONSHDLR_EAGERFREQ,
                                       CONSHDLR_NEEDSCONS,
                                       consEnfolpEdgeConflicts,
                                       consEnfopsEdgeConflicts,
                                       consCheckEdgeConflicts,
                                       consLockEdgeConflicts,
                                       nullptr));
    debug_assert(conshdlr);

    // Set callbacks.
    SCIP_CALL(SCIPsetConshdlrDelete(scip,
                                    conshdlr,
                                    consDeleteEdgeConflicts));
    SCIP_CALL(SCIPsetConshdlrExitsol(scip,
                                     conshdlr,
                                     consExitsolEdgeConflicts));
    SCIP_CALL(SCIPsetConshdlrCopy(scip,
                                  conshdlr,
                                  conshdlrCopyEdgeConflicts,
                                  consCopyEdgeConflicts));
    SCIP_CALL(SCIPsetConshdlrTrans(scip,
                                   conshdlr,
                                   consTransEdgeConflicts));
    SCIP_CALL(SCIPsetConshdlrSepa(scip,
                                  conshdlr,
                                  consSepalpEdgeConflicts,
                                  consSepasolEdgeConflicts,
                                  CONSHDLR_SEPAFREQ,
                                  CONSHDLR_SEPAPRIORITY,
                                  CONSHDLR_DELAYSEPA));

    // Done.
    return SCIP_OKAY;
}

SCIP_RETCODE edge_conflicts_add_var(
    SCIP* scip,                 // SCIP
    SCIP_CONS* cons,            // Edge conflicts constraint
    SCIP_VAR* var,              // Variable
    const Time path_length,     // Path length
    const Edge* const path      // Path
)
{
    // Get constraint data.
    debug_assert(cons);
    auto consdata = reinterpret_cast<EdgeConflictsConsData*>(SCIPconsGetData(cons));
    debug_assert(consdata);
    auto& conflicts = consdata->conflicts;

    // Check.
    debug_assert(var);
    debug_assert(SCIPconsIsTransformed(cons));
    debug_assert(SCIPvarIsTransformed(var));

    // Add rounding lock to the new variable.
    SCIP_CALL(SCIPlockVarCons(scip, var, cons, FALSE, TRUE));

    // Add variable to constraints.
    for (auto [row, edges, t] : conflicts)
    {
#ifdef USE_WAITEDGE_CONFLICTS
        if ((t < path_length - 1 && (path[t] == edges[0] || path[t] == edges[1] || path[t] == edges[2])) ||
            (t >= path_length - 1 && path[path_length - 1].n == edges[2].n))
#else
        if (t < path_length - 1 && (path[t] == edges[0] || path[t] == edges[1]))
#endif
        {
            SCIP_CALL(SCIPaddVarToRow(scip, row, var, 1.0));
        }
    }

    // Return.
    return SCIP_OKAY;
}

const Vector<EdgeConflict>& edge_conflicts_get_constraints(
    SCIP_ProbData* probdata    // Problem data
)
{
    auto cons = SCIPprobdataGetEdgeConflictsCons(probdata);
    debug_assert(cons);
    auto consdata = reinterpret_cast<EdgeConflictsConsData*>(SCIPconsGetData(cons));
    debug_assert(consdata);
    return consdata->conflicts;
}
