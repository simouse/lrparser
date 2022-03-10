// This file is generated by help.sh and help.txt
const char *help_message = 1 + R"(
This program reads a possibly LR grammar from <Grammar file>, takes test 
sequence from standard input stream, and stores analysis results into 
<Result Dir>.

Usage: lrparser -h|--help
       lrparser [-t<Type>] [-g<Grammar file>] [-o<Result Dir>]] <FLAGS>

Possible command: lrparser -tslr -g grammar.txt -o results

Grammar file:
  1) Use ! or # to start a line comment.
  2) Token naming is based on C-style variable naming. Besides, \ can appear 
     at the first character of token, and quoted symbols are supported. (See the
     next rule)
  3) " and ' can be used to quote a symbol, e.g. '+'. "'" and '"' are okay, but 
     you may not use them both in one symbol. Spaces in a quoted string are not 
     allowed.
  4) \e, _e, and \epsilon are reserved for epsilon. 
  5) You shouldn't use token $ in grammar file.
  6) Define terminals, the start symbol, and productions as the following 
     example shows. All symbols at the left hand side of productions are 
     automatically defined as non-terminals. The first non-terminal symbol is 
     defined as the start symbol.

     # Define terminals (optional)
     # If you don't want to define terminals, you should remove this line and 
     # not pass "--disable-auto-define" argument.
     TERM :{ID, '(', ')', '+', '*'}

     # Define productions
     # The format is flexible as well as buggy, which is why you need to pay 
     # some attention. At least symbols in the same production body should be 
     # in the same line.
     exp   -> exp '+' term  | term
     term  -> term '*' fac  
            | fac
     fac   -> ID
     fac   -> "(" exp ')'

Options:
  -t        : Choose a parser type. Available: lr0, slr (default), lalr, lr1. 
  -o        : Specify output directory. (Default: ".").
  -g        : Specify grammar file path. (Default: "grammar.txt")
  -h|--help : Output help message and then exit.

Flags:
--no-test : Just generate automatons and parse table. Do not test an input 
            sequence. Program will finish as soon as the table is generated.
--no-pda  : Do not print automaton in results directory.
--no-pda-label:
            Only show index of each node in dumping results.
--body-start=<String>:
            Define the start of a production as the given <String>. The default 
            is "->", but you may want "::=" or ":" if your grammar is written 
            that way.
--strict  : Input token names must conform to rules of grammar file. Without 
            this flag, they are simply space-splitted.
--debug   : Set output level to DEBUG.
--step    : Read <stdin> step by step. If you have to process a very large input
            file, you may need this flag. But without this flag the parser can 
            provide better display for input queue.
--disable-auto-define: 
            All terminals must be defined before being used.
)";