{-       2+3
                           expr
                        /   |   \
                     term   +   expr
                       |         |
                     factor     term
                       |         | 
                     nat        factor
                       |         |
                       2        nat
                                 |   
                                 3 
                                          
                                          
         2*3*4
                           expr
                            |
                           term
                        /   |   \ 
                     factor *   term
                       |      /   |   \ 
                      nat  factor *   term
                       |      |        |
                       2     nat      factor
                              |        | 
                              3       nat 
                                       | 
                                       4
        
         (2+3)+4
                           expr
                        /    |    \
                      term   +    expr
                       |           |
                      factor      term 
                       |           |  
                      expr        factor
                  /    |   \       |
                term   +  expr    nat
                 |          |      |
                factor    term     4
                 |          |  
                nat       factor
                 |          |
                 2         nat
                            | 
                            3                                                  
-}
