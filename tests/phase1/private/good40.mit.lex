3 IDENTIFIER kStretchTreeDepth
3 =
3 INTLITERAL 14
3 ;
5 IDENTIFIER Node
5 =
5 fun
5 (
5 )
5 {
6 IDENTIFIER this
6 =
6 {
7 IDENTIFIER left
7 :
7 None
7 ;
8 IDENTIFIER right
8 :
8 None
8 ;
9 IDENTIFIER setLeft
9 :
9 fun
9 (
9 IDENTIFIER leftNode
9 )
9 {
10 IDENTIFIER this
10 .
10 IDENTIFIER left
10 =
10 IDENTIFIER leftNode
10 ;
11 }
11 ;
12 IDENTIFIER setRight
12 :
12 fun
12 (
12 IDENTIFIER rightNode
12 )
12 {
13 IDENTIFIER this
13 .
13 IDENTIFIER right
13 =
13 IDENTIFIER rightNode
13 ;
14 }
14 ;
15 IDENTIFIER getLeft
15 :
15 fun
15 (
15 )
15 {
16 return
16 IDENTIFIER this
16 .
16 IDENTIFIER left
16 ;
17 }
17 ;
18 IDENTIFIER getRight
18 :
18 fun
18 (
18 )
18 {
19 return
19 IDENTIFIER this
19 .
19 IDENTIFIER right
19 ;
20 }
20 ;
21 }
21 ;
22 return
22 IDENTIFIER this
22 ;
23 }
23 ;
26 IDENTIFIER maketree
26 =
26 fun
26 (
26 IDENTIFIER depth
26 )
26 {
27 if
27 (
27 IDENTIFIER depth
27 <=
27 INTLITERAL 0
27 )
27 {
28 IDENTIFIER newNode
28 =
28 IDENTIFIER Node
28 (
28 )
28 ;
29 return
29 IDENTIFIER newNode
29 ;
30 }
30 else
30 {
31 IDENTIFIER newNode
31 =
31 IDENTIFIER Node
31 (
31 )
31 ;
32 IDENTIFIER newNode
32 .
32 IDENTIFIER setLeft
32 (
32 IDENTIFIER maketree
32 (
32 IDENTIFIER depth
32 -
32 INTLITERAL 1
32 )
32 )
32 ;
33 IDENTIFIER newNode
33 .
33 IDENTIFIER setRight
33 (
33 IDENTIFIER maketree
33 (
33 IDENTIFIER depth
33 -
33 INTLITERAL 1
33 )
33 )
33 ;
34 return
34 IDENTIFIER newNode
34 ;
35 }
36 }
36 ;
39 IDENTIFIER print
39 (
39 STRINGLITERAL "Garbage collector bench5.mit"
39 )
39 ;
41 IDENTIFIER print
41 (
41 STRINGLITERAL "Stretching memory with a binary tree of depth "
41 +
41 IDENTIFIER kStretchTreeDepth
41 )
41 ;
42 IDENTIFIER temp
42 =
42 IDENTIFIER maketree
42 (
42 IDENTIFIER kStretchTreeDepth
42 )
42 ;
43 IDENTIFIER temp
43 =
43 None
43 ;
