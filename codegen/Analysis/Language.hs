{-# LANGUAGE ViewPatterns #-}
{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE TypeOperators   #-}
{-# LANGUAGE PatternSynonyms #-}




module Analysis.Language
  
where


import PPrint
import Data.Coerce
import Data.Ord      (comparing)
import Data.Char     (isUpper)
import Data.Function (on)
import Data.Map      (Map)
import GHC.Exts      (IsString (..))
import Control.Monad.State 

import qualified
       Data.Map as M

type TyVar = String
type TyCon = String


data DataDef a = DDef
  { dType   :: Type
  , constrs :: [Constructor a]
  }
  deriving (Show)

data Constructor a = DCon
  { conName :: ID
  , conArgs :: [Type]
  }
  deriving (Show)

instance Eq (Constructor a) where
  (==) = (==) `on` conName
  
instance Ord (Constructor a) where
  compare = comparing conName

data Type
  = TFun Type Type
    -- ^ Function type: @t1 -> t2@. Could also be thought of as (TApp (TApp "->" t1) t2).

  | TVar TyVar
    -- ^ polymorphic type variable.
    
  | TPrim PrimType
    -- ^ Primitive types.
    
  | TApp TyCon [Type]
    -- ^ Type application. Not sure if it's worth separating out a constructor
    -- for non-parameterized types or just allowing things like @TApp "Bool" []@
    
  deriving (Show)

instance Unparse Type where
  unparse t = case t of
    TFun t1 t2 -> let wrap | TFun{} <- t1 = parens
                           | otherwise = id
                  in wrap (unparse t1) <+> arw <+> unparse t2
    TVar v     -> text v
    TPrim pt   -> unparse pt
    TApp c ts  -> text c <+> hsep (map maybeParen ts)
      where maybeParen t = case t of
              TFun{} -> parens (unparse t)
              TApp{} -> parens (unparse t)
              _      -> unparse t

-- Probably not going to expand on this, but who knows ...
data PrimType = PInt deriving (Show)

instance Unparse PrimType where
  unparse PInt = text "I#"


newtype Prog a b = Prog { unprog :: ([ValDef' a b], [DataDef a]) }
  deriving (Show)

data ValDef' a b = VDef
  { binding :: ID
  , rhsval  :: Expr' a b
  }
  deriving (Show)


pattern FunDef id parms expr meta = VDef id (Lambda parms expr meta)

data Expr' a b
  = Lit    { lval  :: Literal
           , meta  :: b
           }
  | Var    { name  :: ID
           , meta  :: b
           }
  | Lambda { parms :: [ID]
           , expr  :: Expr' a b
           , meta  :: b
           }
  | CaseOf { scrut :: Expr' a b
           , bind  :: ID
           , paths :: [Clause' a b]
           , meta  :: b
           }
  | LetRec { binds :: [ValDef' a b]
           , expr  :: Expr' a b
           , meta  :: b
           }
  | Apply  { efun  :: Expr' a b 
           , earg  :: Expr' a b
           , meta  :: b
           }
  deriving (Show)



unfoldAp e = go e [] []
  where go (Apply e1 e2 m) args metas = go e1 (e2:args) (m:metas)
        go ef args metas = (ef, args, metas)

data Literal = UBInt Int deriving (Show)

type ValDef a = ValDef' () a
type Expr a   = Expr' () a
type Clause a = Clause' () a

data Clause' a b
  = LitMatch { lpat :: Literal
               -- ^ Can match on primitive literals
             , consq :: Expr' a b
             }
  | ConMatch { cpat :: ID
               -- ^ could refer to a var or construct, both are uniquely identified
             , args :: [ID]
               -- ^ newly scoped variables matching constructor parameters
             , consq :: Expr' a b
             }
  | Default  { consq :: Expr' a b
             }
  deriving (Show)


data ID = ID
  { occName :: String -- ^ similar to GHC: How the name occured in the program
  , uniq    :: Int    -- ^ The /real/ unique identifier for this element.
  }
  deriving (Show, Eq)

looksLikeCon :: ID -> Bool
looksLikeCon (occName -> x:_) = isUpper x
looksLikeCon _ = False

looksLikeFun :: ID -> Bool
looksLikeFun = not . looksLikeCon

instance Ord ID where
  compare = comparing uniq




type SimpleProg a = Prog (a :-> Uniquified :-> SatCons :-> SatFuns)
type UniqProg a   = Prog (a :-> Uniquified)

data a :-> b
data Uniquified
data SatFuns
data SatCons
  

ensureSimpleAST :: UniqProg a b -> SimpleProg a b
ensureSimpleAST = ensureSatFuns . ensureSatCons



-- Check to make sure all functions are fully applied

ensureSatFuns :: Prog a b -> Prog (a :-> SatFuns) b
ensureSatFuns (Prog (vdefs, ddefs)) = Prog (newVDefs, newDDefs)
  where newDDefs   = coerce ddefs
        funArities = mkFunArityMap vdefs M.empty
        newVDefs   = map (satFunBind funArities) vdefs

mkFunArityMap vdefs amap = foldr addLambda amap vdefs
  where addLambda (FunDef id ps _ _) amap = M.insert id (length ps) amap
        addLambda (VDef id _) amap = M.delete id amap

satFunBind arities (VDef b rhs) = VDef b $ satFunExpr arities rhs
satFunExpr arities exp = case exp of
  Lit v m -> Lit v m
  Var n m -> Var n m
  Lambda parms ex meta -> Lambda parms (satFunExpr newMap ex) meta
    where newMap = foldr M.delete arities parms
          
  CaseOf scrut bnd paths meta -> CaseOf newScrut bnd newPaths meta
    where withBindMap = M.delete bnd arities
          newScrut = satFunExpr arities scrut
          newPaths = map satFunClause paths          
          satFunClause c = case c of
            Default e       -> Default       $ satFunExpr withBindMap e
            LitMatch l e    -> LitMatch l    $ satFunExpr withBindMap e
            ConMatch c vs e -> ConMatch c vs $ satFunExpr newMap e
              where newMap = foldr M.delete withBindMap (c:vs)
          
  LetRec binds ex meta -> LetRec newBinds newEx meta
    where newBinds = map (satFunBind newMap) binds
          newMap = mkFunArityMap binds arities
          newEx = satFunExpr newMap ex

  Apply _ _ _ -> result
    where (Var name vmeta, eargs, metas) = unfoldAp exp
          newArgs = map (satFunExpr arities) eargs
          zipped = zip newArgs metas
          newApp = foldl (\arg (ap, m) -> Apply ap arg m) newBaseFun zipped
          newBaseFun = Var name vmeta
          result = if not (looksLikeFun name) || saturated
                   then newApp
                   else error $ show name ++
                        " is an unsaturated constructor application"
          -- If we can't find it in the map, assume it's saturated.
          saturated = maybe True (== length eargs) $ M.lookup name arities

          
--------------------------------------------------------------------------------
--   Check to ensure all constructors are fully applied
--------------------------------------------------------------------------------

ensureSatCons ::  Prog a b -> Prog (a :-> SatCons) b
ensureSatCons (Prog (vdefs, ddefs)) = Prog (newVDefs, newDDefs)
  where
    conArities = M.fromList $ concatMap (map getArity . constrs) ddefs
    getArity (DCon name args) = (name, length args)
    newDDefs = coerce ddefs -- don't need to do anything to datadefs
    newVDefs = map (satConBind conArities) vdefs


satConBind :: Map ID Int -> ValDef' a b ->  ValDef' (a :-> SatCons) b
satConBind arities (VDef b rhs) = VDef b $ satConExpr arities rhs

satConExpr :: Map ID Int -> Expr' a b -> Expr' (a :-> SatCons) b
satConExpr arities exp = case exp of
  Lit v m -> Lit v m
  Var n m -> Var n m
  Lambda parms ex meta ->
    Lambda parms (satConExpr arities ex) meta

  CaseOf scrut bnd paths meta -> CaseOf newScrut bnd newPaths meta
    where newScrut = satConExpr arities scrut
          newPaths = map satConPath paths
          satConPath m = case m of
            LitMatch l e    -> LitMatch l    $ satConExpr arities e
            ConMatch c vs e -> ConMatch c vs $ satConExpr arities e
            Default  e      -> Default       $ satConExpr arities e

  LetRec binds ex meta -> LetRec newBinds newEx meta
    where newBinds = map (satConBind arities) binds
          newEx   = satConExpr arities ex

  Apply _ _ _ -> result
    where (Var name vmeta, eargs, metas) = unfoldAp exp
          newArgs = map (satConExpr arities) eargs
          zipped = zip newArgs metas
          newApp = foldl (\arg (ap, m) -> Apply ap arg m) newBaseFun zipped
          newBaseFun = Var name vmeta
          result = if not (looksLikeCon name) || saturated
                   then newApp
                   else error $ show name ++
                        " is an unsaturated constructor application"
          -- If we can't find it in the map, assume it's saturated.
          saturated = maybe True (== length eargs) $ M.lookup name arities
    

--------------------------------------------------------------------------------
-- Assign unique integers to every variable introduced to a scope, update the
-- identifiers in subsequent nodes with that value.
--------------------------------------------------------------------------------
type UniqState a = State (Map String Int, Int) a

uniquify :: Prog a b -> UniqProg a b
uniquify (Prog (vdefs, ddefs)) = Prog (newVDefs, newDDefs)
  where (newDDefs, st) = runState (mapM uniqDDef ddefs) (M.empty, 0)
        newVDefs = evalState (uniqVDefs vdefs) st


-- | Ensure any new bindings resulting from a stateful Uniqification are
-- discarded afterwards.  This is required when new bindings may shadow old.
scoped :: UniqState a -> UniqState a
scoped s = do
  (umap, _) <- get
  v <- s
  (_ , i) <- get
  put (umap, i)
  pure v

-- | given an 'ID', set its unique integer, if unset.  The value either comes
-- from the map, if the name has already been given a unique value, or is given
-- the next available.  This assumes the map holds values that appropriately
-- represent the current scope this ID resides within.
setUniq :: ID -> UniqState ID
setUniq i@(ID name uniq)
  | uniq /= -1 = pure i
  | otherwise = do
      (umap, cur) <- get      
      case M.lookup name umap of
        Nothing -> error $ show name ++  " not in scope!"
        Just v  -> put (umap, cur) >> pure (ID name v)

-- | Given an 'ID', add it to the map and assign it a new uniq value.  This is
-- for introducing a new scope, such as in 'LetRec', or 'CaseOf' expressions
newScope :: ID -> UniqState ID
newScope (ID name u) = do
  (umap, cur) <- get
  put (M.insert name cur umap, cur + 1)
  pure (ID name cur)


uniqDDef :: DataDef a -> UniqState (DataDef (a :-> Uniquified))
uniqDDef (DDef ty constrs) = DDef ty <$> mapM uniqConstr constrs

uniqConstr :: Constructor a -> UniqState (Constructor (a :-> Uniquified))
uniqConstr (DCon id ty) = DCon <$> newScope id <*> pure ty
 
uniqVDefs:: [ValDef' a b] -> UniqState [ValDef' (a :-> Uniquified) b]
uniqVDefs defs = mapM (newScope . binding) defs >> mapM oneDef defs
 where oneDef (VDef id rhs) = VDef <$> setUniq id <*> uniqExpr rhs

uniqExpr:: Expr' a b -> UniqState (Expr' (a :-> Uniquified) b)
uniqExpr e = scoped $ case e of
  Lit l m  -> pure $ Lit l m
  Var id m -> Var <$> setUniq id <*> pure m
  Lambda pms e m    ->
    Lambda <$> mapM newScope pms <*> uniqExpr e <*> pure m
  CaseOf scr bnd pths m ->
    CaseOf <$> uniqExpr scr <*> newScope bnd
             <*> mapM uniqClause pths <*> pure m
  LetRec bnds exp m ->
    LetRec <$> uniqVDefs bnds <*> uniqExpr exp <*> pure m
  Apply f e m ->
    Apply <$> uniqExpr f <*> uniqExpr e <*> pure m
  
uniqClause :: Clause' a b -> UniqState (Clause' (a :-> Uniquified) b)
uniqClause c = scoped $ case c of
  LitMatch l e -> LitMatch l <$> uniqExpr e
  ConMatch c vs e -> ConMatch c <$> mapM newScope vs <*> uniqExpr e
  Default e -> Default <$> uniqExpr e
  