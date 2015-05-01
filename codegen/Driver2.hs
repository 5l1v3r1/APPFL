module Driver2 (
  renamer,
  normalizer,
  freevarer,
  infotaber,
  conmaper,
--  codegener
) where

import           ADT
import           Analysis
import           AST
import           CodeGen
import           ConMaps2IT
import           InfoTab
import           HeapObj
import           Parser
import           Rename
import           SetFVs

import           Data.List
header :: String
header = "#include \"stg_header.h\"\n"
        
footer :: Bool -> String
footer v = 
  let top = "\nDEFUN0(start) {\n" ++
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
            "  CALL0_0(start);\n"
      bot = "  return 0;\n" ++ "}\n\n"
  in if v then top ++ "  showStgHeap();\n" ++ bot else top ++ bot
           

-- nameDefs
--  :: [([Char], Obj)] ->
--     [([Char], [Char])] ->
--     State [[Char]] [([Char], Obj)]


-- need a better way, like reading from a .h file
stgRTSGlobals :: [String]
stgRTSGlobals = [ "stg_case_not_exhaustive",
                  "true",  -- sho_True
                  "false"] -- sho_False

renamer :: String -> [Def ()]
renamer = renameDefs . parse

normalizer :: String -> [Def ()]
normalizer = normalize . renamer

freevarer :: String -> [Def [Var]]
freevarer inp = let defs = normalizer inp
                in setFVsDefs stgRTSGlobals defs

infotaber :: String -> [Def InfoTab]
infotaber inp = let defs = freevarer inp
                in setITs defs :: [Def InfoTab]

conmaper :: String -> [Def InfoTab]
conmaper = conmaps2IT . infotaber
{-
codegener :: String -> Bool -> String
codegener inp v = let defs = conmaper inp
                      infotab = showITs defs
                      (shoForward, shoDef) = showSHOs defs
                      (funForwards, funDefs) = cgObjs defs stgRTSGlobals
                 in header ++
                    intercalate "\n" funForwards ++ "\n" ++
                    infotab ++ "\n" ++
                    shoForward ++ "\n" ++
                    shoDef ++ "\n" ++
                    intercalate "\n\n" funDefs ++
                    footer v
-}                   
