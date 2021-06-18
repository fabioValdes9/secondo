/*
//paragraph [10] title:    [{\Large \bf ]    [}]
//characters [1] formula:     [$]     [$]
//[ae] [\"{a}]
//[oe] [\"{o}]
//[ue] [\"{u}]
//[ss] [{\ss}]
//[Ae] [\"{A}]
//[Oe] [\"{O}]
//[Ue] [\"{U}]
//[**] [$**$]
//[toc] [\tableofcontents]
//[=>] [\verb+=>+]
//[:Section Translation] [\label{sec:translation}]
//[Section Translation] [Section~\ref{sec:translation}]
//[:Section 4.1.1] [\label{sec:4.1.1}]
//[Section 4.1.1] [Section~\ref{sec:4.1.1}]
//[Figure pog1] [Figure~\ref{fig:pog1.eps}]
//[Figure pog2] [Figure~\ref{fig:pog2.eps}]
//[newpage] [\newpage]

[10] Query Optimization for Distributed Query Processing

Fapra group 2015/16 and Ralf Hartmut G[ue]ting, June 2016.

[toc]

[newpage]

1 Introduction

This file contains additions to ~optimizerNewProperties~ to support query optimization for distributed processing, using the ~Distributed2Algebra~. 


2 The Target Language

In the target language, we use the following additional operators:

----
     dloop     darray(X) x string x  (X->Y) -> darray(Y)
           
                Performs a function on each element of a darray instance.The 
                string argument specifies the name of the result. If the 
                name is undefined or an empty string, a name is generated 
                automatically.

   dloop2     darray(X) x darray(Y) x string x (fun : X x Y -> Z) -> darray(Z)
           
               Performs a function on the elements of two darray instances. 
               The string argument specifies the name of the resulting 
               darray. If the string is undefined or empty, a name is 
               generated automatically.
         
    dmap    d[f]array x string x fun -> d[f]array
           
              Performs a function on a distributed file array. If the 
              string argument is empty or undefined, a name for the result 
              is chosen automatically. If not, the string specifies the 
              name. The result is of type dfarray if the function produces 
              a tuple stream or a relationi; otherwise the result is a 
              darray.

  dmap2    d[f]array x d[f]array x string x fun -> d[f]array
           
             Joins the slots of two distributed arrays.

 partition  d[f]array(rel(tuple)) x string x (tuple->int) x int-> dfmatrix
           
             Redistributes the contents of a dfarray value. The new slot 
             contents are kept on the worker where the values were stored 
             before redistributing them. The last argument (int) 
             determines the number of slots of the redistribution. If 
             this value is smaller or equal to zero, the number of slots 
             is overtaken from the array argument.

partitionF  d[f]array(rel(X)) x string x ([fs]rel(X)->stream(Y)) x (Y ->
            int) x int -> dfmatrix(rel(Y))
           
              Repartitions a distributed [file] array. Before repartition,
              a function is applied to the slots.

 collect2  dfmatrix x string x int -> dfarray
            
            Collects the slots of a matrix into a  dfarray. The string 
            is the name of the resulting array, the int value specified 
            a port for file transfer. The port value can be any port 
            usable on all workers. A corresponding file transfer server 
            is started automatically.
          
  areduce  dfmatrix(rel(t)) x string x (fsrel(t)->Y) x int -> d[f]array(Y)
            
            Performs a function on the distributed slots of an array. 
            The task distribution is dynamically, meaning that a fast 
            worker will handle more slots than a slower one. The result 
            type depends on the result of the function. For a relation 
            or a tuple stream, a dfarray will be created. For other non-
            stream results, a darray is the resulting type.
 
dsummarize    darray(DATA) -> stream(DATA) , d[f]array(rel(X)) -> stream(X)
           
               Produces a stream of the darray elements.
          
  getValue   {darray(T),dfarray(T)} -> array(T)
           
               Converts a distributed array into a normal one.

     tie     ((array t) (map t t t)) -> t
          
                Calculates the "value" of an array evaluating the elements
                of the array with a given function from left to right.
----


3 Replication

To consider distributed queries with predicates containing non-relation
objects, it is necessary to replicate the objects to the 
involved workers. 

For now we assume that every found object is contained in the distributed
part of the query (function of dmap or dmap2).

*/


:- dynamic(replicatedObject/1).
:- dynamic(shared/1).	% has already been shared once in this session


replicateObjects :- not(replicateObjects2).

replicateObjects2 :-
  replicatedObject(X),
  not(shared(X)),
  atom_string(X, XString),
  plan_to_atom(share(value_expr(string, XString), true, 
    dbotherobject(sec2workers, _)), Query),
  atom_concat('query ', Query, Command),
  secondo(Command),
  assert(shared(X)),
  fail.


/*
4 Translation of Plans  

*/

plan_to_atom_string(X, Result) :-
  isDistributedQuery,
  retractall(replicatedObject(_)),
  plan_to_atom(X, Result),
  replicateObjects,
  !.
  
plan_to_atom_string(X, Result) :-
  not(isDistributedQuery),
  plan_to_atom(X, Result),
  !.



% preliminary translation of dproduct (before operator exists)
plan_to_atomD(dproduct(X, Y, _, Plan, Server), Result) :-
  plan_to_atom(X, XAtom),
  plan_to_atom(Y, YAtom),
  plan_to_atom(value_expr(string, ""), S),
  plan_to_atom(Plan, PlanAtom),
  plan_to_atom(Server, ServerAtom),
  atomic_list_concat([XAtom, ' ', YAtom, ' dproduct[', S, ', ', 
    PlanAtom, ', ', ServerAtom, ']'], '', Result).
  

% remember objects to be shared (replicated) in the distributed case, 
% called dbobject

plan_to_atomD(dbobject(Name), ExtName) :-  
  dcName2externalName(DCname, Name),      % convert to DC-spelling
  ( dcName2externalName(DCname, ExtName)  % if Name is known
   -> ( isDistributedQuery -> assertOnce(replicatedObject(ExtName)) ; true )
   -> true
   ; ( write_list(['\nERROR:\tCannot translate \'',dbobject(DCname),'\'.']),
       throw(error_Internal(optimizer_plan_to_atom(dbobject(DCname),
                                                 ExtName)::missingData)),
       fail
     )
  ),
  !.


% define attributes of second argument including renaming

plan_to_atomD(our_attrname(attr(Name, Arg, Case)), Result) :-
  plan_to_atomD(our_a(Name, Arg, Case), Result).

plan_to_atomD(our_a(_:B, _, _), Result) :-
  upper(B, B2),
  atom_concat('..', B2, Result),
  !.

plan_to_atomD(our_a(X, _, _), Result) :-
  upper(X, X2),
  atom_concat('..', X2, Result),
  !.


% just get the attribute name, regardless of renaming

plan_to_atomD(simple_attrname(attr(Name, Arg, Case)), Result) :-
  plan_to_atomD(simple_a(Name, Arg, Case), Result), !.

plan_to_atomD(simple_a(_:B, _, _), B2) :-
  upper(B, B2),
  !.

plan_to_atomD(simple_a(X, _, _), X2) :-
  upper(X, X2),
  !.



/*
5 Translation Rules

5.1 Translation of Arguments

Treat translation into distributed arguments. The properties we use are...
  
  *  ~distribution~ (~DistributionType~, ~DistributionAttribute~, ~DistributionParameter~):

    ~DistributionType~ is ~share~, ~spatial~, ~modulo~, ~function~ or ~random~.

    ~DistributionAttribute~ is the attribute of the relation used to determine
  on which partition(s) to put a given tuple (in theory this could also be a list).

    ~DistributionParameter~ is the parameter used for the distribution (like grid or
  function object / operator).

  * ~distributedobjecttype~(~Type~) (~Type~ is ~darray~, ~dfarray~ or ~dfmatrix~).

  * ~disjointpartitioning~ signals that, if we treat a partition as the multi set
  of the tuples it contains, the union of all partitions is the original relation
  (put differently, in as far as duplicates exist, they have been present in the 
  original relation). 
  
    Since some Secondo plans eliminate duplicates anyways, they can do without their
  arguments having this property (e.g. spatial join).
  
*/

% Translate into object found in SEC2DISTRIBUTED.
distributedarg(N) translatesD [Object, P] :-
  argument(N, Rel),
  Rel = rel(DCName, _),
  distributedRels(rel(DCName, _), Object, DistObjType, _,
    DistType, DistAttr, DistParam),
  X = [distribution(DistType, DCDistAttr, DistParam),
  distributedobjecttype(DistObjType)],
  ( (DistType = spatial) -> P = X 
    ; append(X, [disjointpartitioning], P) 
  ),
  downcase_atom(DistAttr, DCDistAttr).


/* 

5.2 Translation of Selections that Concern Distributed Relations

5.2.1 Selection Without Index

*/

% Generic case. Remove duplicates if needed.
distributedselect(Arg, pr(Cond, rel(_, Var))) translatesD
  [dmap(ArgA, value_expr(string, ""), filter(Plan, Cond2)), P2] :-
  Arg => [ArgA, P],
  %	write('ArgA = '), write(ArgA), nl, nl,
  %	write('P = '), write(P), nl, nl,
  % partitions of the argument relations need to be disjoint
  ( member(disjointpartitioning, P) 
    -> Cond2 = Cond, P2 = P
    ; Cond2 = and(Cond, Original), 
      append(P, [disjointpartitioning], P2)
  ),
  % rename if needed
  % feedRenameRelation(dot, Var, Plan),
  feedRenameRelation2(Arg, dot, Var, Plan),
  renamedRelAttr(attr(original, 1, u), Var, Original).

/*
5.2.2 Using a Standard Index (B-Tree)

*/

% Use btree index for a starts predicate.
distributedselect(arg(N), pr(Attr starts Val, rel(_, Var)))
  translatesD [dmap2(IndexObj, RelObj, value_expr(string, ""), 
    Range2, myPort), 
    [distribution(DistType, DCDistAttr, DistParam), 
       distributedobjecttype(dfarray), disjointpartitioning]] :-
    argument(N, rel(DCName, _)),
    distributedRels(rel(DCName, _), RelObj, _, _,
      DistType, DistAttr, DistParam),
    ( DistType = spatial 
      -> Range2 = filter(Range, Original)  % remove duplicates
      ;  Range2 = Range
    ),
    downcase_atom(DistAttr, DCDistAttr),
    attrnameDCAtom(Attr, DCAttr),
   	write('we got here'), nl, nl, nl, 
    % Lookup a btree index for the relation + attribute
    distributedIndex(RelObj, DCAttr, btree, IndexObj),
    renameStream(range(dot, dotdot, Val, increment(Val)),
      Var, Range),
    renamedRelAttr(attr(original, 1, u), Var, Original),
  !.

% Use btree index for predicate =.
distributedselect(arg(N), pr(Attr = Val, rel(_, Var)))
  translatesD [dmap2(IndexObj, RelObj, value_expr(string, ""), 
    Exactmatch2, myPort), 
    [distribution(DistType, DCDistAttr, DistParam), 
       distributedobjecttype(dfarray), disjointpartitioning]] :-
    argument(N, rel(DCName, _)),
    distributedRels(rel(DCName, _), RelObj, _, _,
      DistType, DistAttr, DistParam),
    ( DistType = spatial 
      -> Exactmatch2 = filter(Exactmatch, Original)  % remove duplicates
      ;  Exactmatch2 = Exactmatch
    ),
    downcase_atom(DistAttr, DCDistAttr),
    attrnameDCAtom(Attr, DCAttr),
   	write('we got here'), nl, nl, nl, 
    % Lookup a btree index for the relation + attribute
    distributedIndex(RelObj, DCAttr, btree, IndexObj),
    renameStream(exactmatch(dot, dotdot, Val), Var, Exactmatch),
    renamedRelAttr(attr(original, 1, u), Var, Original),
  !.

/*
5.2.3 Using a Spatial Index

*/

% Use spatial index for an intersection predicate.
distributedselect(arg(N), pr(Attr intersects Val, rel(_, Var)) )
  translatesD [dmap2(IndexObj, RelObj, value_expr(string, ""), 
    filter(Intersection, Pred), myPort),
    [distribution(DistType, DCDistAttr, DistParam), 
      distributedobjecttype(dfarray), disjointpartitioning]] :-
    argument(N, rel(DCName, _)),
    % We need a materialized argument relation to use the index
    distributedRels(rel(DCName, _), RelObj, _, _,
      DistType, DistAttr, DistParam),
    ( DistType = spatial 
      -> Pred = and(Attr intersects Val, Original)  % remove duplicates
      ;  Pred = (Attr intersects Val)
    ),
    downcase_atom(DistAttr, DCDistAttr),
    % Lookup an rtree index for the relation + attribute
    attrnameDCAtom(Attr, DCAttr),
    distributedIndex(RelObj, DCAttr, rtree, IndexObj),
    renameStream(windowintersects(dot, dotdot, Val),
      Var, Intersection),
    renamedRelAttr(attr(original, 1, u), Var, Original).



/*
5.3 Distributed Join

5.3.1 Distributed Generic Join

*/

% Asymmetric, different orders have different costs.
distributedjoin(Arg1, Arg2, pr(Pred, rel(_, Var1), rel(_, Var2))) 
    translatesD [Plan, P] :-
  Arg1 => [Arg1A, _],		% Arg1A = Arg1Array
  Arg2 => [Arg2A, _],
  Plan = dproduct(Arg1A, Arg2A, e, symmjoin(Arg1S, Arg2S, Pred), myPort),
  P = [distribution(random, *, *), distributedobjecttype(dfarray)],
  feedRenameRelation2(Arg1, dot, Var1, Arg1S),	% Arg1S = Arg1Stream
  feedRenameRelation2(Arg2, dotdot, Var2, Arg2S).

distributedjoin(Arg1, Arg2, pr(Pred, rel(_, Var1), rel(_, Var2))) 
    translatesD [Plan, P] :-
  Arg1 => [Arg1A, _],		 
  Arg2 => [Arg2A, _],
  Plan = dproduct(Arg2A, Arg1A, e, symmjoin(Arg2S, Arg1S, Pred), myPort),
  P = [distribution(random, *, *), distributedobjecttype(dfarray)],
  feedRenameRelation2(Arg2, dot, Var2, Arg1S),		 
  feedRenameRelation2(Arg1, dotdot, Var1, Arg2S).


/*
5.3.2 Equijoin

*/

% Both arguments are NOT distributed for this equijoin. Redistribute both and 
% use areduce.
distributedjoin(Arg1, Arg2, pr(X = Y, rel(_, Var1), rel(_, Var2)))
    translatesD [Plan, P] :-
  Arg1 => [Arg1A, P1],		 
  Arg2 => [Arg2A, P2], 
  X = attr(_, _, _),
  Y = attr(_, _, _),
  isOfFirst(Attr1, X, Y),
  isOfSecond(Attr2, X, Y),
  attrnameDCAtom(Attr1, DCAttr1),
  attrnameDCAtom(Attr2, DCAttr2),
  % repartition each argument 
  not(member(distribution(_, DCAttr1, _), P1)),
  not(member(distribution(_, DCAttr2, _), P2)),
	write('we get here. '), nl, nl,
  feedRenameRelation2(Arg1, dot, Var1, Arg1S),	
  feedRenameRelation2(Arg2, dotdot, Var2, Arg2S),
  InnerPlan = hashjoin(Arg1S, Arg2S, attrname(Attr1), attrname(Attr2), 999997),
  Plan = areduce2(
    partitionF(Arg1A, value_expr(string, ""), feed(dot), 
        hashvalue(our_attrname(X), 999997), 0),
    partitionF(Arg2A, value_expr(string, ""), feed(dot), 
        hashvalue(our_attrname(X), 999997), 0),
    value_expr(string, ""),
    InnerPlan, myPort),
	write('The plan is (areduce2): '), write(Plan), nl,
  P = [distribution(function, DCAttr1, hash), 
    distribution(function, DCAttr2, hash),
    distributedobjecttype(dfarray)].
  

% At least one argument is already distributed for this equijoin. Distribute 
% the other one if needed.
distributedjoin(Arg1, Arg2, pr(X = Y, rel(_, Var1), rel(_, Var2)))
    translatesD [Plan, P] :-
  Arg1 => [Arg1A, P1],		 
  Arg2 => [Arg2A, P2], 
  X = attr(_, _, _),
  Y = attr(_, _, _),
  isOfFirst(Attr1, X, Y),
  isOfSecond(Attr2, X, Y),
  attrnameDCAtom(Attr1, DCAttr1),
  attrnameDCAtom(Attr2, DCAttr2),
  % repartition each argument if necessary
  ( member(distribution(_, DCAttr1, _), P1) -> Arg1B = Arg1A
    ;
    Arg1B = collect2(
      partitionF(Arg1A, value_expr(string, ""), feed(dot), 
        hashvalue(our_attrname(X), 999997), 0),
      value_expr(string, ""), myPort)
  ),
  ( member(distribution(_, DCAttr2, _), P2) -> Arg2B = Arg2A
    ;
    Arg2B = collect2(
      partitionF(Arg2A, value_expr(string, ""), feed(dot), 
        hashvalue(our_attrname(X), 999997), 0),
      value_expr(string, ""), myPort)
  ),
  InnerPlan = hashjoin(Arg1S, Arg2S, attrname(Attr1), attrname(Attr2), 999997),
  Plan = dmap2(Arg1B, Arg2B, value_expr(string, ""), InnerPlan, myPort),
  P = [distribution(function, DCAttr1, hash), 
    distribution(function, DCAttr2, hash),
    distributedobjecttype(dfarray)],
  feedRenameRelation2(Arg1, dot, Var1, Arg1S),	  % Arg1S = Arg1Stream
  feedRenameRelation2(Arg2, dotdot, Var2, Arg2S).
  
  
/*
5.3.3 Spatial Join

*/

% Distribute arguments for spatial join as needed.
distributedjoin(Arg1, Arg2, pr(X intersects Y, rel(_, Var1), rel(_, Var2)))
    translatesD [Plan, P] :-
  Arg1 => [Arg1A, P1],		 
  Arg2 => [Arg2A, P2], 
  X = attr(_, _, _),
  Y = attr(_, _, _),
  isOfFirst(Attr1, X, Y),
  isOfSecond(Attr2, X, Y),
  attrnameDCAtom(Attr1, DCAttr1),
  attrnameDCAtom(Attr2, DCAttr2),
  unrenamedAttr(Attr1, Attr1u), 
  unrenamedAttr(Attr2, Attr2u),
  % repartition each argument if necessary
  ( member(distribution(_, DCAttr1, _), P1) -> Arg1B = Arg1A
    ;
    Arg1B = collect2(
      partitionF(Arg1A, value_expr(string, ""), 
        extendstream(feed(dot), field(attr(cell, 1, u), 
          cellnumber(bbox(Attr1u), grid))),
        CellDistAttr1, 0),
      value_expr(string, ""), myPort)
  ),
  ( member(distribution(_, DCAttr2, _), P2) -> Arg2B = Arg2A
    ;
    Arg2B = collect2(
      partitionF(Arg2A, value_expr(string, ""), 
        extendstream(feed(dot), field(attr(cell, 2, u), 
          cellnumber(bbox(Attr2u), grid))),
        CellDistAttr2, 0),
      value_expr(string, ""), myPort)
  ),
  % rename the cell attribute if needed
  renamedRelAttr(attr(cell, 1, u), Var1, CellAttr1),
  renamedRelAttr(attr(cell, 2, u), Var2, CellAttr2),
  renamedRelAttr2(Arg1, attr2(cell, 1, u), Var1, CellDistAttr1),
  renamedRelAttr2(Arg2, attr2(cell, 2, u), Var2, CellDistAttr2),
  InnerPlan = 
    filter(
      itSpatialJoin(Arg1S, Arg2S, attrname(Attr1), attrname(Attr2)),
      ((CellAttr1 = CellAttr2) and (X intersects Y)) and 
        gridintersects(grid, bbox(X), bbox(Y), CellAttr1) ),
  Plan = dmap2(Arg1B, Arg2B, value_expr(string, ""), InnerPlan, myPort),
  P = [distribution(spatial, DCAttr1, grid), 
    distribution(spatial, DCAttr2, grid),
    distributedobjecttype(dfarray)],
  feedRenameRelation2(Arg1, dot, Var1, Arg1S),		
  feedRenameRelation2(Arg2, dotdot, Var2, Arg2S).
  

 
/*
6 Cost Functions

----    costD(+Arg, +Sel, +Pred, -Size, -NSlots, -Cost) :-
----

The cost of argument ~Arg~ for given selectivity ~Sel~ and predicate ~Pred~ is ~Cost~, the resulting dfarray has ~NSlots~ slots, each with a relation of size ~Size~.

Costs are in microseconds, as for standard cost estimation.

6.1 Preliminary Definitions for Cost Estimation

*/

moveCost(NSlots, Size, X) :-
  X is NSlots * Size * 10.0.	% Wild guess, means ten seconds 
                                % for a million tuples.
				% To be studied, also tuple size plays a role.

partitionCost(NSlots, Size, X) :-
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  PerSlot is Size * 2.0,
  X is NRounds * PerSlot.	% Guess, means two seconds are needed to 
                                % distribute a million tuples on one worker. 
                                % To be studied.

collectCost(NSlots, Size, X) :-
    X is NSlots * Size * 2.0.	% Guess, means two seconds are needed to  
                                % read a column with a million tuples.

nWorkers(14).	% Preliminary.

/*

6.2 Arguments

*/

% Distributed base object
costD(Obj, _, _, Size, NSlots, 0) :-
  distributedRels(rel(DCName, _), Obj, _, NSlots, _, _, _),
  cost(rel(DCName, _), _, _, RelSize, _),
  Size is RelSize / NSlots.

% Intermediate result
costD(res(N), _, _, Size, NSlots, 0) :-
  cost(res(N), _, _, ResSize, _),
  nslots(N, NSlots),
  Size is ResSize / NSlots.
  


/*
6.2 dproduct

Product moves for each slot ~i~ of the first argument all slots of the second argument to the worker processing slot ~i~. There it applies the parameter plan to the relation of slot ~i~ and the complete relation of the second argument.

*/

costD(dproduct(X, Y, _, Plan, _), Sel, Pred, Size, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
	% SizeX is the number of tuples per slot.
	% NSlots is the number of slots of the distributed array.
	% CostX is the cost to produce this argument.
  costD(Y, 1, _, SizeY, NSlots, CostY),
  SizeB is SizeY * NSlots,
  substituteSubterm(dot, dot(SizeX), Plan, Plan1),
  substituteSubterm(dotdot, dotdot(SizeB), Plan1, Plan2),
  	write('Plan cost product = '), write(Plan2), nl, nl,
  cost(Plan2, Sel, Pred, Size, PlanCost), 
  moveCost(NSlots, SizeB, CMove),
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  Cost is CostX + CostY + CMove + NRounds * PlanCost.

/*
6.3 dmap, dmap2

*/

costD(dmap(X, _, Plan), Sel, Pred, Size, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  substituteSubterm(dot, dot(SizeX), Plan, Plan2),
  	write('Plan cost dmap = '), write(Plan2), nl, nl,
	write('Sel = '), write(Sel), nl,
	write('Pred = '), write(Pred), nl, nl,
  cost(Plan2, Sel, Pred, Size, PlanCost), 
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  Cost is CostX + NRounds * PlanCost.

% cost for join operations
costD(dmap2(X, Y, _, Plan, _), Sel, Pred, Size, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  costD(Y, 1, _, SizeY, NSlots, CostY),
  substituteSubterm(dot, dot(SizeX), Plan, Plan1),
  substituteSubterm(dotdot, dotdot(SizeY), Plan1, Plan2),
  	write('Plan cost dmap2 = '), write(Plan2), nl,
	write('Sel = '), write(Sel), nl,
	write('Pred = '), write(Pred), nl, nl,
  Sel2 is Sel * NSlots,
  cost(Plan2, Sel2, Pred, Size, PlanCost), 
        write('The cost of the inner plan is: '), write(PlanCost), nl,
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  Cost is CostX + CostY + NRounds * PlanCost.

% cost for index access. Index is first argument, relation is second.
costD(dmap2(_, X, _, Plan, _), Sel, Pred, Size, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  substituteSubterm(dotdot, dotdot(SizeX), Plan, Plan2),
  	write('Plan cost dmap2 = '), write(Plan2), nl,
	write('Sel = '), write(Sel), nl,
	write('Pred = '), write(Pred), nl, nl,
  Sel2 is Sel,
  cost(Plan2, Sel2, Pred, Size, PlanCost), 
        write('The cost of the inner plan is: '), write(PlanCost), nl,
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  Cost is CostX + NRounds * PlanCost.






/*
6.4 partitionF, collect2

*/

costD(partitionF(X, _, _, _, _), _, _, SizeX, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  partitionCost(NSlots, SizeX, Cost2),
  Cost is CostX + Cost2.

costD(collect2(X, _, _), _, _, SizeX, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  collectCost(NSlots, SizeX, Cost2),
  Cost is CostX + NRounds * Cost2.


/*
6.5 areduce2

*/

costD(areduce2(X, Y, _, Plan, _), Sel, Pred, Size, NSlots, Cost) :-
  costD(X, 1, _, SizeX, NSlots, CostX),
  costD(Y, 1, _, SizeY, NSlots, CostY),
  collectCost(NSlots, SizeX, CostX1),
  collectCost(NSlots, SizeY, CostY1),
  substituteSubterm(dot, dot(SizeX), Plan, Plan1),
  substituteSubterm(dotdot, dotdot(SizeY), Plan1, Plan2),
  	write('Plan2 areduce2 = '), write(Plan2), nl, nl,
  Sel2 is Sel * NSlots,
  cost(Plan2, Sel2, Pred, Size, PlanCost), 
  nWorkers(NWorkers),
  NRounds is (NSlots // NWorkers) + 1,
  Cost is CostX + CostY + NRounds * (CostX1 + CostY1 + PlanCost).



/*
7 Combining Sequential and Distributed Operations

The plan created by conjunctive query optimization consists of distributed operations. These are followed by sequential operations such as projection, groupby, etc. This version of plan is not yet correct.

The following predicates transform a mixed distributed and sequential plan into a correct distributed plan closed by sequential operations.

*/

/*
---- transformDPlan(+Plan, -Plan2) :-
----

Transform a preliminary plan ~Plan~ into a ~Plan2~ composed correctly of distributed and sequential operations.

*/


% special treatment of pure counting query on a relation, plan is
% already finished by queryToPlan.
transformDPlan(Plan, Plan) :-
  Plan = tie(_, _).



transformDPlan(Plan, Plan2) :-
	write('Here is the plan to be transformed: '), nl,
        write(Plan), nl, nl,
  transform2DPlan(Plan, DistributedPlan, SequentialPlan),
        nl, write('The distributed plan is: '), nl,
        nl, write(DistributedPlan), nl,
        nl, write('The sequential plan is: '), nl,
        nl, write(SequentialPlan), nl,
  combinePlans(DistributedPlan, SequentialPlan, Plan2),
        nl, write('The resulting plan is: '), nl,
        nl, write(Plan2), nl, nl, nl.



/*
---- transform2DPlan(+Plan, -DistributedPlan, -SequentialPlan) :-
----

Transform a given preliminary mixed plan ~Plan~ into a distributed and a sequential plan, which still need to be combined. Combining means to embed the distributed plan as an initial part of the sequential one. 

The predicate recursively processes the given mixed plan, adding for each operation the respective distributed and/or sequential operations as appropriate.

*/




transform2DPlan(consume(Plan), DistPlan, consume(SeqPlan)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

% grouping may create new attributes in the distributed part, so we cannot 
% move the projection in this case. It is also not needed, hence is ignored.
transform2DPlan(project(Plan, _), DistPlan, SeqPlan) :-
  Plan = groupby(_, _, _), 
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(project(Plan, Attrs), 
    dmap(DistPlan, value_expr(string, ""), project(feed(dot), Attrs)), 
    SeqPlan) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(extend(Plan, NewAttrs), 
    dmap(DistPlan, value_expr(string, ""), extend(feed(dot), NewAttrs)), 
    SeqPlan) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(head(sortby(Plan, Args), N), 
    dmap(DistPlan, value_expr(string, ""), 
      head(sortby(feed(dot), Args), N)), 
    head(sortby(SeqPlan, Args), N)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(head(Plan, N), 
    dmap(DistPlan, value_expr(string, ""), head(feed(dot), N)), 
    head(SeqPlan, N)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(groupby(sortby(Plan, Order), GroupAttrs, Fields),
    dmap(DistPlan, value_expr(string, ""), 
      groupby(sortby(feed(dot), Order), GroupAttrs, DistFields)),
    groupby(sortby(SeqPlan, Order), GroupAttrs, SeqFields)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  transformDFields(Fields, SeqFields),
  transformDFieldsDist(Fields, DistFields),
  !.

transform2DPlan(sortby(Plan, Args), DistPlan, sortby(SeqPlan, Args)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(count(Plan), 
    dmap(DistPlan, value_expr(string, ""), count(feed(dot))), 
    tie(getValue(seqstart), dot + dotdot)) :-
  transform2DPlan(Plan, DistPlan, seqstart),
  !.

transform2DPlan(count(Plan), DistPlan, count(SeqPlan)) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(Plan, Plan, seqstart) :-
  (   Plan = dmap(_, _, _) 
    ; Plan = dmap2(_, _, _, _, _)
    ; Plan = dmap(dbotherobject(_, _), _, _)
    ; Plan = dproduct(_, _, _, _, _)
    ; Plan = areduce2(_, _, _, _, _)
  ),
  !.

transform2DPlan(predinfo(Plan, _, _), DistPlan, SeqPlan) :-
  transform2DPlan(Plan, DistPlan, SeqPlan),
  !.

transform2DPlan(dbotherobject(X, Y), dbotherobject(X, Y), seqstart) :-
  !.

transform2DPlan(Plan, Plan, mygoodness) :-
  write('transform2DPlan. I cannot transform this: '), nl,
  write(Plan),
  !.



transformDFields([Field], [Field2]) :-
  transformDField(Field, Field2),
  !.

transformDFields([Field | Fields], [Field2, Fields2]) :-
  transformDField(Field, Field2),
  transformDFields(Fields, Fields2),
  !.

transformDField(
  field(NewName, min(feed(group), _)),
  field(NewName, min(feed(group), attrname(NewName)))) :-
  !.

transformDField(
  field(NewName, max(feed(group), _)),
  field(NewName, max(feed(group), attrname(NewName)))) :-
  !.

transformDField(
  field(NewName, sum(feed(group), _)),
  field(NewName, sum(feed(group), attrname(NewName)))) :-
  !.

transformDField(
  field(NewName, count(feed(group))),
  field(NewName, sum(feed(group), attrname(NewName)))) :-
  !.

transformDField(
  field(NewName, avg(feed(group), _)),
  field(NewName, sum(feed(group), attrname(NewNameSum)) / 
    sum(feed(group), attrname(NewNameCnt))) ) :-
  modify(NewName, 'Sum', NewNameSum),
  modify(NewName, 'Cnt', NewNameCnt),
  !.



transformDFieldsDist(
    [field(NewName, avg(feed(group), Attr)) | Fields],
    [field(NewNameSum, sum(feed(group), Attr)), 
      field(NewNameCnt, count(feed(group))) | Fields2]) :-
  transformDFieldsDist(Fields, Fields2),
  modify(NewName, 'Sum', NewNameSum),
  modify(NewName, 'Cnt', NewNameCnt),
  !.

transformDFieldsDist( [Field | Fields], [Field | Fields2]) :-
  transformDFieldsDist(Fields, Fields2),
  !.
  
transformDFieldsDist(
    [field(NewName, avg(feed(group), Attr))],
    [field(NewNameSum, sum(feed(group), Attr)), 
      field(NewNameCnt, count(feed(group)))]) :-
  modify(NewName, 'Sum', NewNameSum),
  modify(NewName, 'Cnt', NewNameCnt),
  !.

transformDFieldsDist([Field], [Field]) :-
  !.

modify(attr(Name, 0, u), X, attr(Name2, 0, u)) :-
  atom_concat(Name, X, Name2).


/*
----    combinePlans(+DistributedPlan, +SequentialPlan, -Plan) :-
----

Embed the distributed into the sequential plan including dsummarize if appropriate. Also merge distributed operations whenever possible.

*/


% special case: counting query with tie. No dsummarize.
combinePlans(DistributedPlan, SequentialPlan, Plan) :-
  SequentialPlan = tie(_, _), !,
  mergeDmaps(DistributedPlan, DistributedPlan2),
  substituteSubterm(seqstart, DistributedPlan2, SequentialPlan, Plan).

% general case. Use dsummarize.
combinePlans(DistributedPlan, SequentialPlan, Plan) :-
  	nl, write('DistributedPlan = '), write(DistributedPlan), nl,
  	nl, write('SequentialPlan = '), write(SequentialPlan), nl,
  mergeDmaps(DistributedPlan, DistributedPlan2),
  substituteSubterm(seqstart, dsummarize(DistributedPlan2), 
    SequentialPlan, Plan),
  	nl, write('Plan = '), write(Plan), nl.
  


/*
8 Merging Distributed Operations

Adjacent dmap operations can be merged. To be extended for ~dmap2~, ~areduce~, ~partitionF~. 

----    mergeDmaps(+Plan, -Plan2)
----

Descends into terms and merges dmaps if possible.

*/

% final dmaps

% following dmap2
mergeDmaps(
  dmap(X, _, OuterPlan),
  dmap2(X1, Y1, value_expr(string, ""), Plan, FS))
  :-
	% write('second dmap, 1'), nl,
  mergeDmaps(X, dmap2(X1, Y1, _, InnerPlan, FS)),
	% write('second dmap, 2'), nl,
  substituteSubterm(feed(dot), InnerPlan, OuterPlan, Plan),
	% write('second dmap, 3'), nl,
  !.

% following dmap
mergeDmaps(
  dmap(X, _, OuterPlan),
  dmap(X1, value_expr(string, ""), Plan))
  :-
  % write('first dmap, 1'), write('     '), write('X = '), write(X), nl,
  mergeDmaps(X, dmap(X1, _, InnerPlan)), 
  % write('first dmap, 2'), nl, write('     '), write('X1 = '), write(X1), nl,
  substituteSubterm(feed(dot), InnerPlan, OuterPlan, Plan),
	% write('first dmap, 3'), nl,
  !.



% dmap2, following dmap

% dmap2 for index access
mergeDmaps(
  dmap(dmap2(dbdistindexobject(Index), Rel, _, InnerPlan, FileServer), _, 
    OuterPlan),
  dmap2(dbdistindexobject(Index), Rel, value_expr(string, ""), Plan, 
    FileServer)) :-
  substituteSubterm(feed(dot), InnerPlan, OuterPlan, Plan),
  !.

% dmap2 for joins
mergeDmaps(
  dmap(dmap2(X, Y, _, InnerPlan, FileServer), _, OuterPlan),
  dmap2(X1, Y1, value_expr(string, ""), Plan, FileServer)) :-
  mergeDmaps(
    dmap2(X, Y, _, InnerPlan, FileServer),
    dmap2(X1, Y1, _, InnerPlan1, FileServer)),
  substituteSubterm(feed(dot), InnerPlan1, OuterPlan, Plan),
  !.


% dmap2, preceding dmaps and other operations

% dmap2 for index access
mergeDmaps(
  dmap2(dbdistindexobject(Index), Rel, _, Plan, FileServer),
  dmap2(dbdistindexobject(Index), Rel, value_expr(string, ""), Plan, 
    FileServer)) :-
  !.


% dmap2 for joins. Each argument can be dmap or something else, e.g. collect2 
% or simply an argument.
mergeDmaps(
  dmap2(X, Y, _, OuterPlan, FileServer),
  dmap2(XArg, YArg, value_expr(string, ""), Plan2, FileServer) )
  :-
  mergeDmaps(X, XPlan), 
  ( XPlan =  dmap(X1, _, InnerPlanX)
    -> substituteSubterm(feed(dot), InnerPlanX, OuterPlan, Plan1),
       XArg = X1
    ;  XArg = XPlan, Plan1 = OuterPlan
  ),
  mergeDmaps(Y, YPlan), 
  ( YPlan =  dmap(Y1, _, InnerPlanY)
    -> substituteSubterm(feed(dot), feed(dotdot), InnerPlanY, InnerPlanY2),
       substituteSubterm(feed(dotdot), InnerPlanY2, Plan1, Plan2),
       YArg = Y1
    ;  YArg = YPlan, Plan2 = Plan1
  ),
  !.


% dproduct, preceding dmaps
mergeDmaps(
  dproduct(X, Y, _, OuterPlan, FileServer),
  dproduct(X1, Z, value_expr(string, ""), Plan, FileServer)) :-
  mergeDmaps(X, dmap(X1, _, InnerPlan)),
  mergeDmaps(Y, Z), 
  substituteSubterm(feed(dot), InnerPlan, OuterPlan, Plan),
  !.

% dproduct, following dmaps
mergeDmaps(
  dmap(dproduct(X, Y, _, InnerPlan, FileServer), _, OuterPlan),
  dproduct(X1, Y1, value_expr(string, ""), Plan, FileServer)) :-
  mergeDmaps(
    dproduct(X, Y, _, InnerPlan, FileServer),
    dproduct(X1, Y1, _, InnerPlan1, FileServer)),
  substituteSubterm(feed(dot), InnerPlan1, OuterPlan, Plan),
  !.


% areduce2 + partitionF, preceding dmaps
mergeDmaps(
  areduce2(
    partitionF(X, _, InnerPlanX, Func, FS),
    partitionF(Y, _, InnerPlanY, Func, FS), 
    _, Outerplan, N),
  areduce2(
    partitionF(X1, value_expr(string, ""), InnerPlanX1, Func, FS),
    partitionF(Y1, value_expr(string, ""), InnerPlanY1, Func, FS), 
    value_expr(string, ""), Outerplan, N)) :-
	write('areduce2 0'), nl, 
  mergeDmaps(
    partitionF(X, _, InnerPlanX, Func, FS), 
    partitionF(X1, _, InnerPlanX1, Func, FS)),
	write('areduce2 1'), nl, 
  mergeDmaps(
    partitionF(Y, value_expr(string, ""), InnerPlanY, Func, FS), 
    partitionF(Y1, value_expr(string, ""), InnerPlanY1, Func, FS)),
	write('areduce2 2'), nl, 
  !.


% areduce, following dmaps
mergeDmaps(
  dmap(areduce2(X, Y, S, InnerPlan, FS), S, OuterPlan),
  areduce2(X1, Y1, S, Plan, FS))
  :-
  mergeDmaps(
    areduce2(X, Y, S, InnerPlan, FS),
    areduce2(X1, Y1, S, InnerPlan1, FS)),
  substituteSubterm(feed(dot), InnerPlan1, OuterPlan, Plan),
  !.
  

% partitionF, preceding dmaps
mergeDmaps(
  partitionF(dmap(X, S, InnerPlan), S, OuterPlan, Func, FS),
  partitionF(X1, S, Plan, Func, FS))
  :-
  mergeDmaps(dmap(X, S, InnerPlan), dmap(X1, S, InnerPlan1)),
  substituteSubterm(feed(dot), InnerPlan1, OuterPlan, Plan),
  !.
  

% collect2
mergeDmaps(
  collect2(X, _, N),
  collect2(X1, value_expr(string, ""), N)) :-
  mergeDmaps(X, X1),
  !.


  
mergeDmaps(Plan, Plan) :-
  ( Plan = dmap(dbotherobject(_, _), _, _) 
    ; Plan = dmap2(_, _, _, _, _) 	% this case to be improved
    ; Plan = dproduct(_, _, _, _, _)
  ),
  !.

mergeDmaps(Plan, Plan).

/*
Yet to be done:

  * Merge a preceding dmap on first or second argument into dmap2.

  * Merge a preceding dmap on first argument only (!) into dproduct (because second argument is moved before execution starts and could be reduced in dmap before movement).
 
*/



/*
8 Check for Distributed Queries

Checks if all relations are distributed. Currently the 
optimizer can only handle queries including relations, that 
are all local or distributed. Situations with mixed 
relation types will be discarded.

*/

%handle not distributed queries
checkDistributedQuery :-
   not(isDistributedQuery),
   isLocalQuery,
   !.
   
checkDistributedQuery :-
   isDistributedQuery,
   not(isLocalQuery),
   !.
   
checkDistributedQuery :- 
  write('Error in query: not all relations distributed '),
  fail,
  !.


/*
8 Check the Spelling of Non-Relation Objects 
 
*/

spelledObj(Term, Obj, Type, l) :-
  downcase_atom(Term, DcObj),
  objectCatalog(DcObj, LcObj, Type),
  LcObj = lc(Obj),
  !.

spelledObj(Term, Obj, Type, u) :-    
  downcase_atom(Term, DcObj),
  objectCatalog(DcObj, Obj, Type), 
  !.
  
spelledObj(_, _, _, _) :- !, fail.  % no entry, avoid backtracking.


/*
9 Auxiliary Predicates

*/

%fapra 15/16

% Extract parts from a query
destructureQuery(Select from Rel where Pred, Select, Rel, Pred).

% Pred is a predicate about the value of an attribute being equal to given value
attrValueEqualityPredicate(Pred, Value, Attr, Rel) :-
  Pred = pr(Value = Attr, Rel),
  Attr = attr(_, _, _).

attrValueEqualityPredicate(Pred, Value, Attr, Rel) :-
  Pred = pr(Attr = Value, Rel),
  Attr = attr(_, _, _).

/*

----   substituteSubterm(Substituted, Substitute, OriginalTerm, TermWithSubstitution)
----

Substituting ~Substituted~ for ~Substitute~ on ~OriginalTerm~ yields ~TermWithSubstitution~. We have a cut in every clause to remove unnecessary choice points
during the search for planedges, which is driven by meta predicates.

*/

% The whole term is to be substituted:
substituteSubterm(Substituted, Substitute, Substituted, Substitute):- !.

% The whole term doesn't match and it's not compound:
substituteSubterm(Substituted, _, OriginalTerm, OriginalTerm) :-
  functor(OriginalTerm, _, 0),
  OriginalTerm \= Substituted, !.

% The whole term doesn't match and it's compount - dive into its subterms:
substituteSubterm(Substituted, Substitute, OriginalTerm, 
  TermWithSubstitution) :-
  functor(OriginalTerm, Functor, Arity),
  functor(TermWithSubstitution, Functor, Arity),
  substituteSubtermInNthSubterm(Arity, Substituted, 
    Substitute, OriginalTerm, TermWithSubstitution), !.

% Terminal case. All subterms have been processed.
substituteSubtermInNthSubterm(0, _, _, _, _):- !.

% Generic case. Process nth subterm.
substituteSubtermInNthSubterm(N, Substituted, Substitute, 
  OriginalTerm, TermWithSubstitution) :-
  not(N = 0),
  arg(N, OriginalTerm, OriginalNthTerm),
  substituteSubterm(Substituted, Substitute, 
    OriginalNthTerm, NthTermWithSubstitution),
  arg(N, TermWithSubstitution, NthTermWithSubstitution),
  Next is N - 1,
  substituteSubtermInNthSubterm(Next, Substituted, 
    Substitute, OriginalTerm, TermWithSubstitution), !.


/*
Rename an attribute to match the renaming of its relation.

*/

% No renaming needed.
renamedRelAttr(RelAttr, Var, RelAttr) :-
  Var = *, !.

renamedRelAttr(attr(Name, N, C), Var, attr(Var:Name, N, C)).




renamedRelAttr2(Arg, RelAttr, Var, RelAttr) :-
  ( Var = * ; Arg = arg(_) ),
  !.

renamedRelAttr2(_, attr(Name, N, C), Var, attr(Var:Name, N, C)).

renamedRelAttr2(_, attr2(Name, N, C), Var, attr2(Var:Name, N, C)).






% Extract the down case name from an attr term.
attrnameDCAtom(Attr, DCAttrName) :-
  Attr = attr(_:Name, _, _),
  !,
  atom_string(AName, Name),
  downcase_atom(AName, DCAttrName).

attrnameDCAtom(Attr, DCAttrName) :-
  Attr = attr(Name, _, _),
  atom_string(AName, Name),
  downcase_atom(AName, DCAttrName).


unrenamedAttr(attr(_:Name, N, C), attr(Name, N, C)) :-
  !.

unrenamedAttr(attr(Name, N, C), attr(Name, N, C)).




/*
Rename a tuple a stream.

*/

% No renaming needed.
renameStream(Stream, Var, Plan) :-
  Var = *,
  !,
  Plan = Stream.

renameStream(Stream, Var, rename(Stream, Var)).

/*
Transform a relation to a tuple stream and rename it.

*/

% No renaming needed.
feedRenameRelation(Rel, Var, Plan) :-
  Var = *,
  !,
  Plan = feed(Rel).

feedRenameRelation(Rel, Var, Plan) :-
  Plan = rename(feed(Rel), Var).

feedRenameRelation(rel(Rel, Var), Plan) :-
  feedRenameRelation(Rel, Var, Plan),!.




feedRenameRelation2(Arg, Rel, Var, Plan) :-
  Arg = arg(_),
  Var \= *,   
  Plan = rename(feed(Rel), Var),
  !.

feedRenameRelation2(_, Rel, _, feed(Rel)).












/*
11 Extensions to File ~database.pl~

11.1 Auxiliary Predicates

*/

[library(apply)].

:-
  dynamic(isDistributedQuery/0),
  dynamic(isLocalQuery/0).

/* 
Strip a string off its opening and closing quote. 

*/

stringWithoutQuotes(Str, StrQuoteless) :-
%  string_to_atom(Str, StrAtom),
  atom_string(StrAtom, Str),
  string_concat(X, '\"', StrAtom),
%  string_to_atom(X, XAtom),
  atom_string(XAtom, X),
  string_concat('\"', StrQuoteless , XAtom).

stringWithoutQuotes(Str, Str) :-
  not(string(Str)),!.

/*
Removes the suffix '\_d' from ~DRel~ indicating a distributed relation. If the 
relation is not listed in SEC2DISTRIBUTED the unchanged name is returned in
Variable ~ORel~

*/  
removeDistributedSuffix(DRel as _, ORel) :-
    removeDistributedSuffix(DRel, ORel),!.

removeDistributedSuffix(DRel, ORel) :-
    atom(DRel),
    atom_concat(X,'_d', DRel),
    atom_string(ORel, X),
    isDistributedRelation(rel(ORel, _)),!,
    assertOnce(isDistributedQuery),
    checkDistributedSystem.


removeDistributedSuffix(ORel, DRel) :-
    ORel = DRel,
    !,
    assertOnce(isLocalQuery).

/* 
Ensure to assert a fact only once.

*/

assertOnce(Fact) :-
    not(Fact),!,
    assert(Fact).

assertOnce(_).


/*
11.2 Creating a list of database objects. 

We assume that the object name starts with an capital
 letter. If not an lc()- functor indicates that the 
initial letter is written in lower case. The rest of the
identifier is written mixed case.

*/

:- 
  dynamic(storedObject/3).
  
objectCatalog(DcObj, Obj, Type) :-
  storedObject(DcObj, Obj, Type),
  !.

objectCatalog(DcObj, LcObj, Type) :-
  getSecondoList(ObjList),
  member(['OBJECT',Obj,_,[Type|_]], ObjList),
  downcase_atom(Obj, DcObj),
  is_lowerfl(Obj),
  LcObj = lc(Obj),
  assert(storedObject(DcObj, LcObj, Type)),
  !.

objectCatalog(DcObj, FlObj, Type) :-
  getSecondoList(ObjList),
  member(['OBJECT',Obj,_,[Type|_]], ObjList),
  downcase_atom(Obj, DcObj),
  not(is_lowerfl(Obj)),
  lowerfl(Obj,FlObj),
  assert(storedObject(DcObj, FlObj, Type)),
  !.


/*
11.3 Reading the catalogue of distributed relations

Get metainformation about the distributed relations in this db.
Use distributedRels/7 predicate in conjuction with isDistributedQuery
to cover special cases for distributed queries. 

*/

:-
  dynamic(storedDistributedRelation/7),
  dynamic(onlineWorker/3).

% distributedRels(Rel, Obj, DistObjType, NSlots, PartType, DistAttr) :-
%   distributedRels(Rel, Obj, DistObjType, NSlots, PartType, DistAttr, _).

distributedRels(rel(Rel, Var), ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam) :-
    storedDistributedRelation(_, _, _, _, _, _, _), 
    ground(Var), !,% first argument instantiated - but do not match against Var
    storedDistributedRelation(rel(Rel, _), ObjName, 
    DistObjType, NSlots, PartType, DistAttr, DistParam).

distributedRels(rel(Rel, Var), ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam) :-
    storedDistributedRelation(_, _, _, _, _, _, _), !,
    storedDistributedRelation(rel(Rel, Var), ObjName, 
    DistObjType, NSlots, PartType, DistAttr, DistParam).

distributedRels(rel(Rel, Var), ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam) :-
    not(storedDistributedRelation(_, _, _, _, _, _, _)),
    ground(Var), !,% first argument instantiated - but do not match against Var
    queryDistributedRels, !,
    checkDistributedSystem,
    storedDistributedRelation(rel(Rel, _), ObjName, 
    DistObjType, NSlots, PartType, DistAttr, DistParam).

distributedRels(rel(Rel,Var), ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam) :-
    not(storedDistributedRelation(_, _, _, _, _, _, _)),
    queryDistributedRels, !,
    checkDistributedSystem,
    storedDistributedRelation(rel(Rel, Var), ObjName, 
    DistObjType, NSlots, PartType, DistAttr, DistParam).


%check whether the relation is distributed or not
isDistributedRelation(rel(Rel, _)) :-
  distributedRels(rel(Rel,'*'), _, _, _, _, _, _),
  !.

/* 

Read the values from SEC2DISTRIBUTED relation and store it to a
dynamic predicate.
Values of string attributes are passed to us as atoms with an 
opening and closing quote and have to be stripped off these.
 
*/

storeDistributedRels([]).

storeDistributedRels([[RelName, ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam]|T]) :-
  storeDistributedRel(RelName, ObjName, DistObjType, NSlots, 
    PartType, DistAttr, DistParam), 
  storeDistributedRels(T).

storeDistributedRel(RelName, ObjName, DistObjType, NSlots, 
  PartType, DistAttr, DistParam) :-
  downcase_atom(RelName, DCRelName),
  downcase_atom(ObjName, DCObjName),
  term_to_atom(DOType, DistObjType),
  assert(storedDistributedRelation(rel(DCRelName, '*'), 
         dbotherobject(DCObjName, DOType), 
         DOType, NSlots, PartType, DistAttr, DistParam)),
  !.

storeDistributedRel(_, _, _, _, _) :- !.

spelledDistributedRel(Rel, Rel2, Case) :-
    spelled(Rel, Rel2, Case);
    (ansi_format([fg(red)], 'Warning: listed object "~w" in SEC2DISTRIBUTED \c
      relation does not exist => ignored for further processing \n',[Rel]),
      fail),
    !.
    
/*
----	storeDRels :- 
----

Lookup distributed relations represented as ~drel~ in the Secondo catalog and create the corresponding ~storedDistributedRelation~ facts.

*/

storeDRels :-
  secondoCatalogInfo(_, Name, _, [[drel, [rel, [tuple, Attrs]], _]]),
  my_concat_atom([Rel, 'DR', _], '_', Name),
  downcase_atom(Name, DCObjName),
  downcase_atom(Rel, DCRel),
  atom_concat('query ', Name, QueryAtom),
  secondo(QueryAtom, DRel),
  DRel = [_, [[_, NSlots, _], Distribution]],
  distr(Distribution, Attrs, PartType, DistAttr, DistParam),
  assert(storedDistributedRelation(rel(DCRel, '*'), 
         dbotherobject(DCObjName, drel), 
         drel, NSlots, PartType, DistAttr, DistParam)),
  fail.
  

% distr(Distribution, Attrs, PartType, DistAttr, DistParam)

distr(['"RANDOM"'], _, "random", "*", "*").

distr(['"HASH"', N], Attrs, "function", DistAttr, "*") :-
  nth0(N, Attrs, [Attr, _]),
  atom_string(Attr, DistAttr).


/*
11.4 Check availability of workers

We only perform two simple checks before the first query is executed:

  * The SEC2WORKERS relation is not empty.
  
  * Connections either exist or can be created, which means monitors are running.

*/
:- dynamic(distributedSystemOK/0).

checkDistributedSystem :-
  distributedSystemOK,
  !.

checkDistributedSystem :-
  checkDatabaseOpen,
  checkWorkers,
  checkConnectivity,
  assert(distributedSystemOK).
  
  
checkDatabaseOpen :-
  isDatabaseOpen,
  !.
  
checkDatabaseOpen :-
  my_concat_atom(
    ['Database is not open.'], '', ErrMsg),
  throw(error_SQL(distributed_checkWorkers::noDatabase::ErrMsg)),
  fail.

  
checkWorkers :-
  secondo('query SEC2WORKERS count', [int, Cnt]),
  Cnt > 0,
  !.
  
checkWorkers :-
  my_concat_atom(
    ['SEC2WORKERS relation is empty.'], '', ErrMsg),
  throw(error_SQL(distributed_checkWorkers::noWorkers::ErrMsg)),
  fail.


checkConnectivity :-
  secondo('query checkConnections() count', [int, Cnt]),
  Cnt > 0,
  !.
  
checkConnectivity :- 
  secondo('query SEC2WORKERS feed head[1] consume', Result),
  Result = [_Type, [[Host, Port, Config]]],
  my_concat_atom(['query connect(', Host, ', ', Port, ', ', Config, ')'], '',
    Query),
  secondo(Query, [bool, true]),
  secondo('query disconnect()', _),
  !.
  
checkConnectivity :-
  my_concat_atom(
    ['RemoteMonitors for distributed workers are not running.'], '', ErrMsg),
  throw(error_SQL(distributed_checkConnectivity::noMonitors::ErrMsg)),
  fail.
 

/* 
The system- relation SEC2DISTRIBUTED contains information about
distributed relations in the opened database. 

SEC2WORKERS is another necessary relation when using distributed
queries. It contains the available workers in the system.

If necessary the two relations will be created without content. 

*/

queryDistributedRels :-
  retractall(storedDistributedRelation(_, _, _, _, _, _, _)),
  distributedRelsAvailable,
  secondo('query SEC2DISTRIBUTED',[_, Tuples]), 
  !,
  maplist(maplist(stringWithoutQuotes), Tuples, ObjList),
  storeDistributedRels(ObjList),
  not(storeDRels),
  !.

distributedRelsAvailable :-
  retractall(storedSecondoList(_)),
  getSecondoList(ObjList),
  ( member(['OBJECT','SEC2DISTRIBUTED',_ | [[[_ | [[_ | [_]]]]]]], ObjList) ->
    true;
    secondo('let SEC2DISTRIBUTED = [const rel(tuple(\c
             [RelName: string,\c 
              ArrayRef: string, \c 
              DistType: string, \c
              NSlots: int, \c
              PartType: string, \c
              PartAttribute: string, \c
              PartParam: string]))value()]',_),
    writeln('Created empty SEC2DISTRIBUTED system-relation \n')
  ),
  ( member(['OBJECT','SEC2WORKERS',_ | [[[_ | [[_ | [_]]]]]]], ObjList) ->
    true;
    secondo('let SEC2WORKERS = [const rel(tuple(\c
             [Host: string,\c 
              Port: int, \c 
              Config: string]))value()]',_),
    writeln('Created empty SEC2WORKERS system-relation \n')
  ),
  ( member(['OBJECT','SEC2DISTINDEXES',_ | [[[_ | [[_ | [_]]]]]]], ObjList) ->
    true;
    secondo('let SEC2DISTINDEXES = [const rel(tuple(\c
             [DistObj: string,\c 
              Attr: string, \c 
              IndexType: string, \c
              IndexObj: string]))value()]',_),
    writeln('Created empty SEC2DISTINDEXES system-relation \n')
  ),
  !.

distributedRelsAvailable :-
   writeln('no open database').

% switch to dynamic predicate sometime in the future

distributedIndex(dbotherobject(DistRelObj, Type), DCAttr, IndexType, 
    dbdistindexobject(IndexObj)) :-
  distributedIndex2(DowncaseAtomTuples),
  member([DistRelObj, DCAttr, IndexType, IndexObj], DowncaseAtomTuples),
  distObjectType(DistRelObj, Type).


distributedIndex2(DowncaseAtomTuples) :-
  secondo('query SEC2DISTINDEXES',[_, Tuples]), !,
  maplist(maplist(stringWithoutQuotes), Tuples, StrippedTuples),
  maplist(maplist(atom_string), AtomTuples, StrippedTuples),
  maplist(maplist(downcase_atom), AtomTuples, DowncaseAtomTuples),
  !.

distObjectType(DistRelObj, drel) :-
  secondoCatalogInfo(_, Name, _, [[drel, _, _]]),
  downcase_atom(Name, DistRelObj),
  !.
  
distObjectType(_, darray).
  


/*
13 Extensions to File ~operators.pl~

Some constants for cost functions.

*/

dloopTC(1.3).
dsummarizeTC(28).
dmapTC(0.12).
dmap2TC(0.07).
dloop2TC(0.2).
shareTC(447).
getValueTC(14.4).
partitionTC(11.8).
partitionFTC(8).
areduceTC(3.1).
collect2TC(0.9).

tieTC(0.006).

bboxTC(0.002).
gridintersectsTC(0.99).
cellnumberTC(0.07).
hashvalueTC(0.001).
rangeTC(10). % copied from leftrange
% windowintersectsTC(0.1). %costs need to be evaluated, taken from optimizer
itSpatialJoinTC(20.0, 0.7). %costs need to be evaluated, taken from optimizer
extendstreamTC(0.0). %costs need to be evaluated
areduce2TC(0.0). %costs need to be evaluated




