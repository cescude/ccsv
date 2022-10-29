#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <csv.h>
#include "arena.h"
#include "vector.h"

void usage() {
  fprintf(stderr, "USAGE csv [-f FILENAME] [-c 1,2,3...]\n\n");
  fprintf(stderr, "  -f FILENAME ...... filename of csv to process\n");
  fprintf(stderr, "  -c COLS .......... comma-separated list of columns to print\n");
  fprintf(stderr, "  -h ............... display this help\n");
  fprintf(stderr, "\nNOTES\n\n");
  fprintf(stderr, "  When -f is omitted, uses STDIN.\n");
  fprintf(stderr, "  When -c is omitted, prints the header info\n");
  fprintf(stderr, "\nEXAMPLES\n\n");
  fprintf(stderr, "  csv -f test.csv # print header list from test.csv\n");
  fprintf(stderr, "  csv -f test.csv -c 1,2,9 # print columns 1,2,9\n");
  fprintf(stderr, "  csv -f test.csv -c 1,5-9 # print columns 1, and 5 through 9\n");
  fprintf(stderr, "  csv -f test.csv -c 9,1-8 # put column 9 on the front\n");
  fprintf(stderr, "  csv -f test.csv -c 1,1,5-8,1 # duplicate column 1 several times\n");
}

#define BUF_SIZE (50)
char buf[BUF_SIZE] = {0};

typedef struct {
  size_t column;         /* Which column does this field represent? */
  char* data;            /* What is the data for this field? */
  size_t len;            /* What is the length of the data? */

   /* If non-zero, that means we need to duplicate this field in the
      output, and this is how far ahead we need to jump */
  size_t skip;
} Field;

typedef struct {
  size_t offset;
  char valid;
} SkipLookup;

/* Used to track what's going on in the csv callbacks */
typedef struct {
  size_t current_column;        /* current column (zero based) */
  size_t current_row;           /* current row (zero based)*/

  SkipLookup *skip_table; /* column index => first field entry for that column */

  Arena *field_mem;             /* Manage saved field memory */
  Field *fields; /* List of fields to print, in order (vector array) */
} State;

void field_end_headermode(void *field, size_t len, void *data) {
  State* s = data;
  if (s->current_row > 0) return; /* Only print the first row */
  s->current_column++;
  fprintf(stdout, "%3zu  ", s->current_column);
  fwrite(field, sizeof(char), len, stdout);
  fputc('\n', stdout);
}

void row_end_headermode(int c, void *data) {
  State* s = data;
  s->current_row++;
}

void process_header(FILE* f, State* s, struct csv_parser* p) {
  size_t bytes_read = 0;
  while (!feof(f) && !s->current_row) {
    bytes_read = fread(buf, sizeof(char), BUF_SIZE, f);
    csv_parse(p, buf, bytes_read, field_end_headermode, row_end_headermode, s);
  }
  csv_fini(p, field_end_headermode, row_end_headermode, s);
}

/* Called when a field is read by the csv parser */
void field_end(void *field, size_t len, void *data) {
  State* s = (State*)data;
  s->current_column++;

  /* Allocate space for the field text */
  char* field_text = aralloc(s->field_mem, len);
  if (field_text == NULL) {
    exit(99);
  }

  memcpy(field_text, field, len);

  /* Are we even printing this column at all? */
  if (s->current_column < veclen(s->skip_table) &&
      s->skip_table[s->current_column].valid) {

    /* Find the first instance of this column in our field list */
    size_t i = s->skip_table[s->current_column].offset;
    s->fields[i].data = field_text;
    s->fields[i].len = len;

    /* Use same data over in duplicated columns (if exist) */
    while (s->fields[i].skip) {
      i += s->fields[i].skip;
      s->fields[i].data = field_text;
      s->fields[i].len = len;
    }
  }
}

/* Called when a row is completed */
void row_end(int c, void *data) {
  State* s = (State*)data;
  s->current_column = 0;
  s->current_row++;
  
  size_t num_fields = veclen(s->fields);
  for (size_t i=0; i<num_fields; i++) {
    if (i > 0) {
      fputc(',', stdout);
    }
    csv_fwrite(stdout, s->fields[i].data, s->fields[i].len);

    /* Clear these out, on the off-chance the next row doesn't have full data */
    s->fields[i].data = NULL;
    s->fields[i].len = 0;
  }

  fputc('\n', stdout);

  arreset(s->field_mem);
}

void process_file(FILE* f, State* s, struct csv_parser* p) {
  size_t bytes_read = 0;
  while (!feof(f)) {
    bytes_read = fread(buf, sizeof(char), BUF_SIZE, f);
    csv_parse(p, buf, bytes_read, field_end, row_end, s);
  }
  csv_fini(p, field_end, row_end, s);
}

/* Extracts column definitions from str, puts them into the fields
   vector. Return 1 on error, 0 on success.

   Allowed input:
     1,2,3
     1-3
     3-1
     1,2-10,1
     3-1,4-10,12-20
*/
int parse_columns(Field** fields_ptr, char* str) {
  Field* fields = *fields_ptr;
  size_t a;
  size_t b;

  while (str != NULL) {
    if ( sscanf(str, "%zu-%zu", &a, &b) == 2 ) {
      
      /* Add all columns in the range */
      int direction = (a < b) ? 1 : -1;
      while (1) {
	fields = vecpush(fields, 1);
	fields[veclast(fields)].column = a;

	if (a==b) break;
	a += direction;
      }
    } else if (sscanf(str, "%zu", &a) == 1) {

      /* Add the single column */
      fields = vecpush(fields, 1);
      fields[veclast(fields)].column = a;
    } else {
      return 1;
    }

    if (str = strchr(str, ',')) {
      str++; 			/* skip the comma */
    }
  }

  *fields_ptr = fields;
  return 0;
}

int main(int argc, char** argv) {
  FILE* f = NULL;
  State s = {0};
  char just_header = 1;
  s.fields = (Field*)vecnew(sizeof(Field), 0);

  Arena a = {0};
  arinit(&a);
  s.field_mem = &a;

  /* Parse arguments... */
  int ch;
  while ((ch = getopt(argc, argv, "hf:c:")) != -1) {
    switch (ch) {
    case 'f':
      f = fopen(optarg, "r");
      break;
      
    case 'c':
      just_header = 0; /* The user specified columns, so we're not in "header" mode */
      if (parse_columns(&s.fields, optarg)) {
	usage();
	exit(1);
      }
      break;
      
    case '?':
    case 'h':
      usage();
      exit(1);
      break;
    }
  }

  if (f == NULL) {
    f = stdin;
  }

  /* Go through our fields and mark duplicates */
  size_t num_fields = veclen(s.fields);
  for (size_t i=0; i<num_fields; i++) {
    for (size_t j=i+1; j<num_fields; j++) {
      if (s.fields[i].column == s.fields[j].column) {
        s.fields[i].skip = j-i; /* i + skip => duplicate column */
        break;
      }
    }
  }

  /* Go through our fields and find the first offset for each column */
  s.skip_table = vecnew(sizeof(SkipLookup), 0);
  for (size_t i=0; i<num_fields; i++) {
    size_t col = s.fields[i].column;

    /* Make sure we have enough size_t's for the skip table */
    if (veclen(s.skip_table) <= col) {
      s.skip_table = vecpush(s.skip_table, 1 + col - veclen(s.skip_table));
    }

    if (s.skip_table == NULL) {
      fprintf(stderr, "Allocation failure\n");
      exit(99);
    }

    if (s.skip_table[col].valid) {
      /* We've already recorded the first field index for this column */
      continue;
    }
    
    s.skip_table[col].valid = 1;
    s.skip_table[col].offset = i;
  }

  struct csv_parser p = {0};
  if (csv_init(&p, 0)) {
    usage();
    exit(1);
  }

  if (just_header) {
    process_header(f, &s, &p);
  } else {
    process_file(f, &s, &p);
  }
  
  csv_free(&p);

  arfree(&a);
  vecfree(s.skip_table);
  vecfree(s.fields);
  
  fclose(f);
  return 0;
}
