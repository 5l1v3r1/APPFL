module Driver (
  renamer,
  normalizer,
  freevarer,
  infotaber,
  conmaper,
  codegener
) where

import           Analysis
import           CodeGen
import           ConMap2
import           InfoTab
import           HeapObj
import           Parser
import           Rename
import           SetFVs

import           Data.List
header :: String
header = "#include \"stg_header.h\"\n"
        
footer :: String
footer = "\nDEFUN0(start) {\n" ++
         "  registerSHOs();\n" ++
         "  stgPushCont(showResultCont);\n" ++
         "  STGEVAL(((PtrOrLiteral){.argType = HEAPOBJ, .op = &sho_main}));\n" ++
         "  STGRETURN0();\n" ++
         "  ENDFUN;\n" ++
         "}\n\n" ++
         "int main (int argc, char **argv) {\n" ++
         "  initStg();\n" ++
         "  initCmm();\n" ++
         "  initGc();\n" ++
         "  CALL0_0(start);\n" ++
         "  showStgHeap();\n" ++
         "  return 0;\n" ++
         "}\n\n"

-- nameDefs
--  :: [([Char], Obj)] ->
--     [([Char], [Char])] ->
--     State [[Char]] [([Char], Obj)]


-- need a better way, like reading from a .h file
stgRTSGlobals :: [String]
stgRTSGlobals = [ "stg_case_not_exhaustive",
                  "true",  -- sho_True
                  "false"] -- sho_False

renamer :: String -> [Obj ()]
renamer = renameObjs . parser

normalizer :: String -> [Obj ()]
normalizer = normalize . renamer

freevarer :: String -> [Obj [Var]]
freevarer inp = let defs = normalizer inp
                in setFVsDefs defs stgRTSGlobals

infotaber :: String -> [Obj InfoTab]
infotaber inp = let defs = freevarer inp
                in setITs defs :: [Obj InfoTab]

conmaper :: String -> [Obj InfoTab]
conmaper = setConMap . infotaber

codegener :: String -> String
codegener inp = let defs = conmaper inp
                    infotab = showITs defs
                    (shoForward, shoDef) = showSHOs defs
                    (funForwards, funDefs) = cgObjs defs stgRTSGlobals
                 in header ++
                    intercalate "\n" funForwards ++ "\n" ++
                    infotab ++ "\n" ++
                    shoForward ++ "\n" ++
                    shoDef ++ "\n" ++
                    intercalate "\n\n" funDefs ++
                    footer
                   
