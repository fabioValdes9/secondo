operator distance alias DISTANCE pattern op ( _ , _ )
operator distancesym alias DISTANCESYM pattern op ( _ , _ , _ )
operator hybriddistance alias HYBRIDDISTANCE pattern op ( _ , _ , _ , _ )
operator gethybriddistanceparams alias GETHYBRIDDISTANCEPARAMS pattern op ()
operator sethybriddistanceparam alias SETHYBRIDDISTANCEPARAM pattern op ( _ , _ )
operator longestcommonsubsequence alias LONGESTCOMMONSUBSEQUENCE pattern op ( _ , _ )
operator topattern alias TOPATTERN pattern _ op
operator toclassifier alias TOCLASSIFIER pattern _ op
operator matches alias MATCHES pattern _ infixop _
operator createtupleindex alias CREATETUPLEINDEX pattern _ op [ _ ]
operator bulkloadtupleindex alias BULKLOADTUPLEINDEX pattern _ op [ _ ]
operator bulkloadtupleindex2 alias BULKLOADTUPLEINDEX pattern _ op
operator tmatches alias TMATCHES pattern _ op [ _ , _ ]
operator indextmatches alias INDEXTMATCHES pattern _ _ op [ _ , _ ]
operator indextmatches2 alias INDEXTMATCHES2 pattern _ _ op [ _ ]
operator indexrewrite alias INDEXREWRITE pattern _ _ op [ _ , _ ]
operator createunitrtree alias CREATEUNITRTREE pattern _ op [ _ ]
operator indexmatches alias INDEXMATCHES pattern _ op [ _ , _ , _ ]
operator filtermatches alias FILTERMATCHES pattern _ op [ _ , _ ]
operator rewrite alias REWRITE pattern op ( _ , _ )
operator multirewrite alias MULTIREWRITE pattern _ op [ _ , _ ]
operator classify alias CLASSIFY pattern op ( _ , _ )
operator indexclassify alias INDEXCLASSIFY pattern _ op [ _ , _ , _ ]
operator createml alias CREATEML pattern op ( _ , _ )
operator createmlrelation alias CREATEMLRELATION pattern op ( _ , _ , _ )
operator createtrie alias CREATETRIE pattern _ op [ _ ]
operator createMaxspeedRaster alias CREATEMAXSPEEDRASTER pattern op( _ , _ , _ )
operator createTileAreas alias CREATETILEAREAS pattern op( _ )
operator restoreTraj alias RESTORETRAJ pattern op ( _ , _ , _ , _ , _ , _ , _ , _ , _ )
operator getPatterns alias GETPATTERNS pattern _ op [ _ , _ , _ , _ , _ ]
operator createfptree alias CREATEFPTREE pattern _ op [ _ , _ , _ ]
operator minefptree alias MINEFPTREE pattern _ op [ _ , _ ]
operator createprojecteddb alias CREATEPROJECTEDDB pattern _ op [ _ , _ , _ ]
operator prefixSpan alias PREFIXSPAN pattern _ op [ _ , _ ]
operator createverticaldb alias CREATEVERTICALDB pattern _ op [ _ , _ , _ ]
operator spade alias SPADE pattern _ op [ _ , _ ]
operator createlexicon alias CREATELEXICON pattern _ op [ _ ]
operator frequencyvector alias FREQUENCYVECTOR pattern op ( _ , _)
operator cosinesim alias COSINESIM pattern op ( _ , _)
operator jaccard alias JACCARD pattern op ( _ , _ )
operator tfidf alias TFIDF pattern op ( _ , _ )
operator createsplsemtraj alias CREATESPLSEMTRAJ pattern op ( _ , _ )
