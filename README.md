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

# Performance Notes & Overall Design

The problem this program has to solve is essentially "Reorder,
duplicate, and/or remove columns from a CSV." The problem includes an
input CSV, and a list identifying which of the input columns should be
printed to stdout (and in what order).

The program starts up by using the output columns list (eg. `-c
1,5,3,2,4,4,2`) to create an array of "Fields" that are effectively
the output rules. So the previous example will create 7 Fields in an
array, like this:

    [0] => Field{column=1}
    [1] => Field{column=5}
    [2] => Field{column=3}
    [3] => Field{column=2}
    [4] => Field{column=4}
    [5] => Field{column=4}
    [6] => Field{column=2}

The input row may look like:

    Cats,Dogs,Mice,Squirrels,Beetles,Deer,Horses,Crickets,Moosen,Fish

The first implementation was, while reading a row, to store each
column's data in memory and tag the field with it:

    [0] => Field{column=1, data=Cats}
    [1] => Field{column=5, data=Beetles}
    [2] => Field{column=3, data=Mice}
    [3] => Field{column=2, data=Dogs}
    [4] => Field{column=4, data=Squirrels}
    [5] => Field{column=4, data=Squirrels}
    [6] => Field{column=2, data=Dogs}

After the row was completely parsed, we can just iterate through the
fields, in-order, and print the results to stdout.

    Cats,Beetles,Mice,Dogs,Squirrels,Squirrels,Dogs

Great! However, in the spirit of "if not optimization, at least
non-pessimization," I noticed that I only need to store the data in
Fields if the Field is being printed out of order. So the first
optimization made was to make an "Easy" mode that is called when all
the Fields are ascending from low-to-high.

Therefore Easy mode is as follows: while reading a row, simply print
the column text to stdout if it appears in our output (no data copy
needed).

This resulted in a huge speedup for cases like `-c 1-10`, and `-c
1-3,5,9-10.` However, at this point I noticed that for the normal
(as-now-renamed) "Full" mode, the same technique could be applied to
the fields *until* a duplicated or out-of-order field was found.

A reminder of the input row:

    Cats,Dogs,Mice,Squirrels,Beetles,Deer,Horses,Crickets,Moosen,Fish

So, for example, `-c 1-10,3-5` can:

* Immediately write Cats and Dogs to stdout
* Write Mice to stdout and store the text in Field 11
* Write Squirrels to stdout and store the text in Field 12
* Write Beetles to stdout and store the text in Field 13
* Immediately write Deer, Horses, Crickets, Moosen, and Fish to stdout
* ...at this point we're done reading the input row...
* Write the stored text Mice, Squirrels, and Beetles to stdout

In order to do this, I modified the Field to also track 1) can the
field be written to stdout immediately (ie., is the field "quick"?),
and 2) does the field appear later in the Field list ("next_idx")?

    [0] => Field{column=1, data=Cats,       quick=1, next_idx=0}
    [1] => Field{column=2, data=Dogs,       quick=1, next_idx=0}
    [2] => Field{column=3, data=Mice,       quick=1, next_idx=10}
    [3] => Field{column=4, data=Squirrels,  quick=1, next_idx=11}
    [4] => Field{column=5, data=Beetles,    quick=1, next_idx=12}
    [5] => Field{column=6, data=Deer,       quick=1, next_idx=0}
    [6] => Field{column=7, data=Horses,     quick=1, next_idx=0}
    [7] => Field{column=8, data=Crickets,   quick=1, next_idx=0}
    [8] => Field{column=9, data=Moosen,     quick=1, next_idx=0}
    [9] => Field{column=10 data=Fish,       quick=1, next_idx=0}
    [10] => Field{column=3, data=Mice,      quick=0, next_idx=0}
    [11] => Field{column=4, data=Squirrels, quick=0, next_idx=0}
    [12] => Field{column=5, data=Beetles,   quick=0, next_idx=0}

(Note that because, by definition, the first field is never "next", I
take a shortcut and use zero to mean "no more copies of this field").

So now I have "Easy" and "Full" modes; although Easy could
theoretically be removed at this point (since passing "-c 1-10" to
Full performs the same algorithm of "write everything as received
without saving for later"), testing the performance showed a clear
improvement in the Easy mode version vs the Full (I can only assume
this is due to smaller code size, fewer tests, whatever).

The next "non-pessimization" was seeing how much time was being spent
calling fputc/fwrite/etc in the tight loops (at least twice per output
field, once for the text and once for the separator...). Since the
code doesn't *need* to write out anything until at least a row is
complete (and really not even that often), I implemented a write
buffer w/ the functions `fb()` (flush), `ws()` (write string), and
`wc()` (write character). This also made huge difference in
performance.

Finally, an untested aspect of the performance for this program came
at the very beginning, by using an arena allocator for the
implementation. Because we have a need to store field data as we read
a row, but have *zero* need for the data beyond that, the program
reuses the same memory for field text that gets reset (without
free'ing) at the end of each row. This eliminates many calls to
`malloc()` and `free()` in the tight loops (resetting the arena memory
for the next row is more-or-less "set one (maybe two) size_t's to zero").

However, because I started out by writing an arena allocator, I'm
unsure of it's impact on performance (ie., there was never a
malloc/free version to compare with).

## Timing

Take from this what you will (running on a not-so-recent laptop)! How
large is our input file (taken from Google's aggregated covid health
datasheet)?

    $ du -h ~/Downloads/aggregated.csv
    21G    /Users/chandler.escude/Downloads/aggregated.csv

How long does it take to `cat` this file?

    $ time cat ~/Downloads/aggregated.csv > /dev/null

    real    0m20.244s
    user    0m0.075s
    sys     0m12.490s

How long does it take do do some amount of work on this file?

    $ time wc ~/Downloads/aggregated.csv
    22756334    60395440 22522933003 aggregated.csv

    real    1m37.734s
    user    1m23.009s
    sys     0m13.437s

What's the best case performance of processing the file? This is
basically testing out the overhead of libcsv, since there's minimal
logic/output being exercised...

    $ time ./csv -f ~/Downloads/aggregated.csv -c1 -p > /dev/null
    22756334 Complete!

    real    3m5.833s
    user    2m42.285s
    sys     0m21.053s

Outputting in "easy" mode (all columns ascending), how long does `csv`
take to process the file?

    $ time ./csv -f ~/Downloads/aggregated.csv -c1-708 -p > /dev/null
    22756334 Complete!

    real    6m23.524s
    user    5m43.154s
    sys     0m35.782s

Outputting in "full" mode (every column is written out of order), how
long does `csv` take to process the file?

    $ time ./csv -f ~/Downloads/aggregated.csv -c708-1 -p > /dev/null
    22756334 Complete!

    real    8m10.491s
    user    7m43.648s
    sys     0m22.092s
