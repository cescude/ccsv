# About

`csv` is similar to `cut` for csv files.

# Synopsis

Get a list of numbered columns from a csv:

    $ csv -f example.csv
    1  Item
    2  Cost
    3  Quantity
    4  Date

Generate a new csv with only columns 1 and 3 included:

    $ csv -f example.csv -c1,3
    "Item","Quantity"
    "Chair","4"
    "Table","1"
    "Cup","10"
    
Specify columns via range:

    $ csv -f example.csv -c4,1-3
    "Date","Item","Cost","Quantity"
    "2022-10-31","Chair","40.00","4"
    "2022-11-01","Table","125.00","1"
    "2022-10-31","Cup","3","10"

Reverse columns via range:

    $ csv -f example.csv -c4-1
    "Date","Quantity","Cost","Item"
    "2022-10-31","4","40.00","Chair"
    "2022-11-01","1","125.00","Table"
    "2022-10-31","10","3","Cup"

Use a different field separator for output:

    $ csv -f example.csv -F';;' -c1,3
    "Item";;"Quantity"
    "Chair";;"4"
    "Table";;"1"
    "Cup";;"10"

    $ csv -f example.csv -Fhello -c1-4
    "Item"hello"Cost"hello"Quantity"hello"Date"
    "Chair"hello"40.00"hello"4"hello"2022-10-31"
    "Table"hello"125.00"hello"1"hello"2022-11-01"
    "Cup"hello"3"hello"10"hello"2022-10-31"

Raw mode (useful for passing through to awk or something):

    $ csv -f example.csv -F' ' -c1-4 -r | column -t
    Item   Cost    Quantity  Date
    Chair  40.00   4         2022-10-31
    Table  125.00  1         2022-11-01
    Cup    3       10        2022-10-31

Print progress on stderr (imagine the number after the command is being dynamically updated...):

    $ csv -f really-large.csv -c1-10,20-200,1-5 -p > processed.csv
    22756334

Finally, you can pass the results from csv into itself to quickly view the
output headers:

    $ csv -f example.csv -c1,3,2 | csv
    1  Item
    2  Quantity
    3  Cost
    
# Compiling

Needs https://github.com/rgamble/libcsv available on the system.

Run make to build.

# Full Options

    USAGE csv [OPTS]

      -f FILENAME ... filename of csv to process
      -c COLS ....... comma-separated list of columns to print
      -r ............ don't quote output ("raw" mode)
      -I DELIM ...... use DELIM as input field separator
      -F SEP......... separate output columns with SEP
      -p ............ display progress on stderr
      -h ............ display this help

    NOTES

      * When -f is omitted, uses STDIN
      * When -c is omitted, prints the header info
      * DELIM must be a single character, but SEP may be a string

    EXAMPLES

      csv -f test.csv # print header list from test.csv
      csv -f test.csv -c 1,2,9 # print columns 1,2,9
      csv -f test.csv -c 1,5-9 # print columns 1, and 5 through 9
      csv -f test.csv -c 9,1-8 # put column 9 on the front
      csv -f test.csv -c 1,1,5-8,1 # duplicate column 1 several times

# Performance Notes

Take from this what you will! How large is our input file?

    $ du -h ~/Downloads/aggregated.csv
     21G    /Users/chandler.escude/Downloads/aggregated.csv

How long does it take to `cat` this file?

    $ time cat ~/Downloads/aggregated.csv > /dev/null

    real    0m3.771s
    user    0m0.074s
    sys     0m2.917s

What's the best case performance of processing the file (just a single
column being output means minimal writing, also there's no
out-of-order columns so we don't need to save any data in memory for
reference later)?

    $ time ./csv -f ~/Downloads/aggregated.csv -c1 -p > /dev/null
    22756334 Complete!

    real    1m34.570s
    user    1m28.852s
    sys     0m4.415s

Outputting in "easy" mode (all columns ascending), how long does `csv`
take to process the file?

    $ time ./csv -f ~/Downloads/aggregated.csv -c1-708 -p > /dev/null
    22756334 Complete!

    real    2m23.736s
    user    2m18.093s
    sys     0m4.822s

Outputting in "full" mode (column data must be saved to memory due to
output columns being written out-of-order), how long does `csv` take
to process the file?

   $ time ./csv -f ~/Downloads/aggregated.csv -c708-1 -p > /dev/null
   22756334 Complete!

   real    3m44.462s
   user    3m37.892s
   sys     0m5.700s
