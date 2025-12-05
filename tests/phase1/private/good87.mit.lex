1 IDENTIFIER _print
1 =
1 IDENTIFIER print
1 ;
2 IDENTIFIER print
2 =
2 fun
2 (
2 IDENTIFIER s
2 )
2 {
3 IDENTIFIER _print
3 (
3 STRINGLITERAL "Otherprint: "
3 +
3 IDENTIFIER s
3 )
3 ;
4 }
4 ;
6 IDENTIFIER depth
6 =
6 INTLITERAL 0
6 ;
8 IDENTIFIER add_depth
8 =
8 fun
8 (
8 )
8 {
9 global
9 IDENTIFIER print
9 ;
10 global
10 IDENTIFIER depth
10 ;
11 IDENTIFIER d
11 =
11 IDENTIFIER depth
11 +
11 STRINGLITERAL ""
11 ;
12 IDENTIFIER oldprint
12 =
12 IDENTIFIER print
12 ;
13 IDENTIFIER print
13 =
13 fun
13 (
13 IDENTIFIER s
13 )
13 {
14 IDENTIFIER oldprint
14 (
14 IDENTIFIER intcast
14 (
14 IDENTIFIER d
14 )
14 +
14 STRINGLITERAL ": "
14 +
14 IDENTIFIER s
14 )
14 ;
15 }
15 ;
16 IDENTIFIER depth
16 =
16 IDENTIFIER depth
16 +
16 INTLITERAL 1
16 ;
17 }
17 ;
19 IDENTIFIER _print
19 (
19 STRINGLITERAL "Should print: Otherprint: hello"
19 )
19 ;
20 IDENTIFIER print
20 (
20 STRINGLITERAL "hello"
20 )
20 ;
22 IDENTIFIER add_depth
22 (
22 )
22 ;
23 IDENTIFIER add_depth
23 (
23 )
23 ;
24 IDENTIFIER add_depth
24 (
24 )
24 ;
25 IDENTIFIER add_depth
25 (
25 )
25 ;
26 IDENTIFIER _print
26 (
26 STRINGLITERAL "Should print: Otherprint: 0: 1: 2: 3: hello"
26 )
26 ;
27 IDENTIFIER print
27 (
27 STRINGLITERAL "hello"
27 )
27 ;
29 IDENTIFIER _print
29 (
29 STRINGLITERAL "Should print: <newline>"
29 )
29 ;
30 IDENTIFIER _print
30 (
30 STRINGLITERAL "\n"
30 )
30 ;
32 IDENTIFIER _print
32 (
32 STRINGLITERAL "Should print: \\"
32 )
32 ;
33 IDENTIFIER _print
33 (
33 STRINGLITERAL "\\"
33 )
33 ;
35 IDENTIFIER _print
35 (
35 STRINGLITERAL "Should print: \\\"\\\"\\\""
35 )
35 ;
36 IDENTIFIER _print
36 (
36 STRINGLITERAL "\\\"\\\"\\\""
36 )
36 ;
