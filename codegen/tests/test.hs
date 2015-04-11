module Main where
import qualified Parser.Test
import qualified Rename.Test
import qualified Scanner.Test

import           Test.Tasty

main :: IO()
main = defaultMain tests

tests :: TestTree
tests = testGroup "Tests" [ Scanner.Test.unitTests
                          , Parser.Test.unitTests
                          , Rename.Test.unitTests
                          ]
