1 IDENTIFIER x
1 =
1 INTLITERAL 3
1 ;
2 if
2 (
2 IDENTIFIER x
2 ==
2 INTLITERAL 3
2 )
2 {
3 IDENTIFIER print
3 (
3 STRINGLITERAL "This should be printed."
3 )
3 ;
4 }
6 if
6 (
6 BOOLEANLITERAL false
6 )
6 {
7 IDENTIFIER print
7 (
7 STRINGLITERAL "This should not be printed."
7 )
7 ;
8 }
8 else
8 {
9 IDENTIFIER print
9 (
9 STRINGLITERAL "This should be printed."
9 )
9 ;
10 }
12 while
12 (
12 IDENTIFIER x
12 >
12 INTLITERAL 0
12 )
12 {
13 IDENTIFIER print
13 (
13 IDENTIFIER x
13 )
13 ;
14 IDENTIFIER x
14 =
14 IDENTIFIER x
14 -
14 INTLITERAL 1
14 ;
15 }
