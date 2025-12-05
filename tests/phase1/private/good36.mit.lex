3 IDENTIFIER kLongLivedTreeDepth
3 =
3 INTLITERAL 16
3 ;
6 IDENTIFIER Node
6 =
6 fun
6 (
6 )
6 {
7 IDENTIFIER this
7 =
7 {
8 IDENTIFIER left
8 :
8 None
8 ;
9 IDENTIFIER right
9 :
9 None
9 ;
10 IDENTIFIER setLeft
10 :
10 fun
10 (
10 IDENTIFIER leftNode
10 )
10 {
11 IDENTIFIER this
11 .
11 IDENTIFIER left
11 =
11 IDENTIFIER leftNode
11 ;
12 }
12 ;
13 IDENTIFIER setRight
13 :
13 fun
13 (
13 IDENTIFIER rightNode
13 )
13 {
14 IDENTIFIER this
14 .
14 IDENTIFIER right
14 =
14 IDENTIFIER rightNode
14 ;
15 }
15 ;
16 IDENTIFIER getLeft
16 :
16 fun
16 (
16 )
16 {
17 return
17 IDENTIFIER this
17 .
17 IDENTIFIER left
17 ;
18 }
18 ;
19 IDENTIFIER getRight
19 :
19 fun
19 (
19 )
19 {
20 return
20 IDENTIFIER this
20 .
20 IDENTIFIER right
20 ;
21 }
21 ;
22 }
22 ;
23 return
23 IDENTIFIER this
23 ;
24 }
24 ;
27 IDENTIFIER populate
27 =
27 fun
27 (
27 IDENTIFIER depth
27 ,
27 IDENTIFIER node
27 )
27 {
28 if
28 (
28 IDENTIFIER depth
28 <=
28 INTLITERAL 0
28 )
28 {
29 return
29 None
29 ;
30 }
30 else
30 {
31 IDENTIFIER depth
31 =
31 IDENTIFIER depth
31 -
31 INTLITERAL 1
31 ;
32 IDENTIFIER newLeft
32 =
32 IDENTIFIER Node
32 (
32 )
32 ;
33 IDENTIFIER newRight
33 =
33 IDENTIFIER Node
33 (
33 )
33 ;
35 IDENTIFIER node
35 .
35 IDENTIFIER setLeft
35 (
35 IDENTIFIER newLeft
35 )
35 ;
36 IDENTIFIER node
36 .
36 IDENTIFIER setRight
36 (
36 IDENTIFIER newRight
36 )
36 ;
38 IDENTIFIER populate
38 (
38 IDENTIFIER depth
38 ,
38 IDENTIFIER node
38 .
38 IDENTIFIER getLeft
38 (
38 )
38 )
38 ;
39 IDENTIFIER populate
39 (
39 IDENTIFIER depth
39 ,
39 IDENTIFIER node
39 .
39 IDENTIFIER getRight
39 (
39 )
39 )
39 ;
40 }
41 }
41 ;
44 IDENTIFIER print
44 (
44 STRINGLITERAL "Garbage collector bench1.mit"
44 )
44 ;
46 IDENTIFIER print
46 (
46 STRINGLITERAL "Creating a long-lived binary tree of depth "
46 +
46 IDENTIFIER kLongLivedTreeDepth
46 )
46 ;
47 IDENTIFIER root
47 =
47 IDENTIFIER Node
47 (
47 )
47 ;
48 IDENTIFIER populate
48 (
48 IDENTIFIER kLongLivedTreeDepth
48 ,
48 IDENTIFIER root
48 )
48 ;
