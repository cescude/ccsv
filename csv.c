#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <csv.h>
#include "arena.h"
#include "vector.h"

#define BUF_SIZE (4<<10)

void usage() {
  fprintf(stderr, "USAGE csv [-f FILENAME] [-c 1,2,3...]\n\n");
  fprintf(stderr, "  -f FILENAME ...... filename of csv to process\n");
  fprintf(stderr, "  -c COLS .......... comma-separated list of columns to print\n");
  fprintf(stderr, "  -p ............... display progress on stderr\n");
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

typedef struct {
  size_t column;         /* Which column does this field represent? */
  char* data;            /* What is the data for this field? */
  size_t len;            /* What is the length of the data? */

  /* If non-zero, then we can output this field immediate without
     storing it! */
  char quick;

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
  
  char show_progress;
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
  s->current_column = 0;	/* Unnecessary, though? */
  s->current_row++;
}

void process_header(FILE* f, State* s, struct csv_parser* p) {
  char buf[BUF_SIZE] = {0};
  size_t bytes_read = 0;
  while (!feof(f) && !s->current_row) {
    bytes_read = fread(buf, sizeof(char), BUF_SIZE, f);
    csv_parse(p, buf, bytes_read, field_end_headermode, row_end_headermode, s);
  }
  csv_fini(p, field_end_headermode, row_end_headermode, s);
}

void field_end_fullmode(void *field, size_t len, void *data) {
  State* s = (State*)data;
  s->current_column++;

  /* Are we even printing this column at all? */
  if (s->current_column < veclen(s->skip_table) &&
      s->skip_table[s->current_column].valid) {

    /* Lookup the first instance of this column in our field list */
    size_t i = s->skip_table[s->current_column].offset;
    if (s->fields[i].quick) {
      if (i > 0) {
	fputc(',', stdout);
      }
      csv_fwrite(stdout, field, len);

      if (!s->fields[i].skip) {
	/* Field isn't referenced again, so we don't need to save its
	   data! */
	return;
      }
    }

    /* Allocate space for the field text */
    char* field_text = aralloc(s->field_mem, len);
    if (field_text == NULL) {
      exit(99);
    }

    memcpy(field_text, field, len);

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

void row_end_fullmode(int c, void *data) {
  State* s = (State*)data;
  s->current_column = 0;
  s->current_row++;
  
  size_t num_fields = veclen(s->fields);
  for (size_t i=0; i<num_fields; i++) {
    if (s->fields[i].quick) continue; /* Already been printed */
    
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

  if (s->show_progress && !(s->current_row%10000)) {
    fprintf(stderr, "\r%zu", s->current_row - (rand()%10000));
  }
}

void process_fullmode(FILE* f, State* s, struct csv_parser* p) {
  char buf[BUF_SIZE] = {0};
  size_t bytes_read = 0;
  while (!feof(f)) {
    bytes_read = fread(buf, sizeof(char), BUF_SIZE, f);
    csv_parse(p, buf, bytes_read, field_end_fullmode, row_end_fullmode, s);
  }
  csv_fini(p, field_end_fullmode, row_end_fullmode, s);
  if (s->show_progress) fprintf(stderr, "\r%zu Complete!\n", s->current_row);
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
  while ((ch = getopt(argc, argv, "hf:c:p")) != -1) {
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

    case 'p':
      s.show_progress = 1;
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

  /*
    Go through our fields and figure which can be marked as
    "quick". "Quick" fields are ones that can be written to stdout
    immediately upon being read. Basically, this boils down to any
    fields that are in order from the start of the list.

    Examples:
    -c 1-10         # all can be printed out immediately
    -c 1-10,3       # 1-10 can be printed immediately, but 3 needs to stored for printing later
    -c 1-5,1-5      # 1-5 can be quick, but all need to be stored for the second print
    -c 1-5,10-12    # all can be printed immediately
    -c 1-5,20,10-12 # 1-5 and 20 can be printed immediately, 10-12 need to be printed from storage
  */
  size_t in_order_max = 0;
  size_t num_quick_fields = num_fields; /* Let's be optimistic! */
  for (size_t i=0; i<num_fields; i++) {

    /* Are we out of the initial run-up? */
    if (s.fields[i].column <= in_order_max) {
      num_quick_fields = i;
      break;
    }
    in_order_max = s.fields[i].column;
  }

  /* Mark all the "quick" fields */
  for (size_t i=0; i<num_quick_fields; i++) {
    if (s.fields[i].column <= in_order_max) {
      s.fields[i].quick = 1;
    } else {
      break;
    }
  }

  /* for (size_t i=0; i<num_fields; i++) { */
  /*   fprintf(stdout, "idx=%zu, col=%zu, quick=%d\n", i, s.fields[i].column, s.fields[i].quick); */
  /* } */

  struct csv_parser p = {0};
  if (csv_init(&p, 0)) {
    usage();
    exit(1);
  }

  if (just_header) {
    process_header(f, &s, &p);
  } else {
    process_fullmode(f, &s, &p);
  }

  csv_free(&p);

  arfree(&a);
  vecfree(s.skip_table);
  vecfree(s.fields);
  
  fclose(f);
  return 0;
}
