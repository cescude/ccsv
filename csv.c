#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <csv.h>
#include "arena.h"
#include "vector.h"

#define BUF_SIZE (4<<10)

char* field_separator = ",";
size_t field_separator_len = 1;

char show_progress = 0;
char raw_output = 0;

#define OBUF_SIZE (4<<20)
char write_buffer[OBUF_SIZE] = {0};
size_t write_buffer_len = 0;

/* flush buffer */
void fb() {
  char* p = write_buffer;
  while (write_buffer_len) {
    size_t c = fwrite(p, sizeof(char), write_buffer_len, stdout);
    if (c) {
      p += c;
      write_buffer_len -= c;
    } else {
      exit(99);
    }
  }
}

/* write string */
void ws(char* str, size_t len, char raw) {
  if (!str || !len) return;

  if (raw) {
    if (write_buffer_len + len >= OBUF_SIZE) {
      fb();
    }
    memcpy(write_buffer + write_buffer_len, str, len);
    write_buffer_len += len;
  } else {
    size_t ql = csv_write(NULL, OBUF_SIZE - write_buffer_len, str, len);
    if (write_buffer_len + ql >= OBUF_SIZE) {
      fb();
    }
    csv_write(write_buffer + write_buffer_len, OBUF_SIZE - write_buffer_len, str, len);
    write_buffer_len += ql;
  }
}

/* write character */
void wc(char c) {
  if (write_buffer_len + 1 >= OBUF_SIZE) {
    fb();
  }

  write_buffer[write_buffer_len++] = c;
}

void usage() {
  fprintf(stderr, "USAGE csv [OPTS]\n\n");
  fprintf(stderr, "  -f FILENAME ... filename of csv to process\n");
  fprintf(stderr, "  -c COLS ....... comma-separated list of columns to print\n");
  fprintf(stderr, "  -r ............ don't quote output (\"raw\" mode)\n");
  fprintf(stderr, "  -I DELIM ...... use DELIM as input field separator\n");
  fprintf(stderr, "  -F SEP......... separate output columns with SEP\n");
  fprintf(stderr, "  -p ............ display progress on stderr\n");
  fprintf(stderr, "  -h ............ display this help\n");
  fprintf(stderr, "\nNOTES\n\n");
  fprintf(stderr, "  * When -f is omitted, uses STDIN\n");
  fprintf(stderr, "  * When -c is omitted, prints the header info\n");
  fprintf(stderr, "  * DELIM must be a single character, but SEP may be a string\n");
  fprintf(stderr, "\nEXAMPLES\n\n");
  fprintf(stderr, "  csv -f test.csv # print header list from test.csv\n");
  fprintf(stderr, "  csv -f test.csv -c 1,2,9 # print columns 1,2,9\n");
  fprintf(stderr, "  csv -f test.csv -c 1,5-9 # print columns 1, and 5 through 9\n");
  fprintf(stderr, "  csv -f test.csv -c 9,1-8 # put column 9 on the front\n");
  fprintf(stderr, "  csv -f test.csv -c 1,1,5-8,1 # duplicate column 1 several times\n");
}

typedef struct {
  size_t column;   /* Which input column does this field represent? */
  char* data;      /* What is the data for this field? */
  size_t len;      /* What is the length of the data? */

  /* If non-zero, then we can output this field immediately without
     storing it! */
  char quick;

  /* If non-zero, that means this column is duplicated in the output
     (and next_idx is where it occurs) */
  size_t next_idx;
} Field;

typedef struct {
  size_t offset;
  char valid;
} SkipLookup;

/* Used to track what's going on in the csv callbacks */
typedef struct {
  size_t current_column;        /* current input column (one based) */
  size_t current_row;           /* current row (zero based)*/

   /* input column => first field index for that column. Subsequent
      fields representing this column can be found with
      Field.next_idx */
  SkipLookup *skip_table;

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

/* All our fields are printed in-order (w/ no duplicates), so we don't
   need to store off any field data! */
void field_end_easymode(void* field, size_t len, void* data) {
  State* s = data;
  s->current_column++;

  /* Are we even printing this column at all? */
  if (s->current_column < veclen(s->skip_table) &&
      s->skip_table[s->current_column].valid) {

    /* Check to see if we're the first printed field... */
    if (s->skip_table[s->current_column].offset > 0) {
      ws(field_separator, field_separator_len, 1);
    }

    ws(field, len, raw_output);
  }
}

void row_end_easymode(int c, void* data) {
  State* s = data;
  s->current_column = 0;
  s->current_row++;
  wc('\n');
  if (show_progress && !(s->current_row%10000)) {
    fprintf(stderr, "\r%zu", s->current_row - (rand()%10000));
  }
}

void process_easymode(FILE* f, State* s, struct csv_parser* p) {
  char buf[BUF_SIZE] = {0};
  size_t bytes_read = 0;
  while (!feof(f)) {
    bytes_read = fread(buf, sizeof(char), BUF_SIZE, f);
    csv_parse(p, buf, bytes_read, field_end_easymode, row_end_easymode, s);
  }
  csv_fini(p, field_end_easymode, row_end_easymode, s);
  if (show_progress) fprintf(stderr, "\r%zu Complete!\n", s->current_row);

  fb();
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
	ws(field_separator, field_separator_len, 1);
      }

      ws(field, len, raw_output);

      if (!s->fields[i].next_idx) {
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
    while (s->fields[i].next_idx) {
      i = s->fields[i].next_idx;
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
      ws(field_separator, field_separator_len, 1);
    }
    
    ws(s->fields[i].data, s->fields[i].len, raw_output);

    /* Clear these out, on the off-chance the next row doesn't have full data */
    s->fields[i].data = NULL;
    s->fields[i].len = 0;
  }

  wc('\n');

  arreset(s->field_mem);

  if (show_progress && !(s->current_row%10000)) {
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
  if (show_progress) fprintf(stderr, "\r%zu Complete!\n", s->current_row);
  fb();
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
int parse_columns(size_t** columns_ptr, char* str) {
  size_t* columns = *columns_ptr;
  size_t a;
  size_t b;

  while (str != NULL) {
    if ( sscanf(str, "%zu-%zu", &a, &b) == 2 ) {
      
      /* Add all columns in the range */
      int direction = (a < b) ? 1 : -1;
      while (1) {
	columns = vecpush(columns, 1);
	columns[veclast(columns)] = a;

	if (a==b) break;
	a += direction;
      }
    } else if (sscanf(str, "%zu", &a) == 1) {

      /* Add the single column */
      columns = vecpush(columns, 1);
      columns[veclast(columns)] = a;
    } else {
      return 1;
    }

    if ((str = strchr(str, ',')) != NULL) {
      str++; 			/* skip the comma */
    }
  }

  *columns_ptr = columns;
  return 0;
}

int parse_options(size_t** columns_ptr, FILE** f, char* delim, int argc, char** argv) {
  int ch;
  while ((ch = getopt(argc, argv, "hf:c:pI:F:r")) != -1) {
    switch (ch) {
    case 'f':
      if (*f) {
	return 1;
      }
      
      *f = fopen(optarg, "r");
      break;
      
    case 'c':
      if (parse_columns(columns_ptr, optarg)) {
	return 1;
      }
      break;

    case 'p':
      show_progress = 1;
      break;

    case 'I':
      if (strlen(optarg) > 1) {
        return 1;
      }
      *delim = optarg[0];
      break;

    case 'F':
      field_separator = optarg;
      field_separator_len = strlen(optarg);
      break;

    case 'r':
      raw_output = 1;
      break;
      
    case '?':
    case 'h':
      return 1;
    }
  }

  if (*f == NULL) {
    *f = stdin;
  }

  return 0;
}

void analyze_fields(State* s) {

  /* Go through our fields and mark duplicate offsets */
  size_t num_fields = veclen(s->fields);
  for (size_t i=0; i<num_fields; i++) {
    for (size_t j=i+1; j<num_fields; j++) {
      if (s->fields[i].column == s->fields[j].column) {
        s->fields[i].next_idx = j;
        break;
      }
    }
  }

  /* Go through our fields and find the first offset for each column */
  for (size_t i=0; i<num_fields; i++) {
    size_t col = s->fields[i].column;

    /* Make sure we have enough size_t's for the skip table */
    if (veclen(s->skip_table) <= col) {
      s->skip_table = vecpush(s->skip_table, 1 + col - veclen(s->skip_table));
    }

    if (s->skip_table == NULL) {
      fprintf(stderr, "Allocation failure\n");
      exit(99);
    }

    if (s->skip_table[col].valid) {
      /* We've already recorded the first field index for this column */
      continue;
    }
    
    s->skip_table[col].valid = 1;
    s->skip_table[col].offset = i;
  }

  size_t prev_column = 0;
  for (size_t i=0; i<num_fields; i++) {
    if (s->fields[i].column <= prev_column) {
      break;
    }
    prev_column = s->fields[i].column;
    s->fields[i].quick = 1;
  }

  /* for (size_t i=0; i<num_fields; i++) { */
  /*   fprintf(stdout, "idx=%zu, col=%zu, quick=%d\n", i, s->fields[i].column, s->fields[i].quick); */
  /* } */
}

int main(int argc, char** argv) {
  FILE* f = NULL;
  char delim = ',';
  Arena a = {0};
  arinit(&a);

  State s = {0};
  s.field_mem = &a;
  s.fields = (Field*)vecnew(sizeof(Field), 0);
  s.skip_table = (SkipLookup*)vecnew(sizeof(SkipLookup), 0);

  {
    size_t* output_columns = (size_t*)vecnew(sizeof(size_t), 0);

    /* Parse arguments... */
    if (parse_options(&output_columns, &f, &delim, argc, argv)) {
      usage();
      exit(1);
    }

    s.fields = vecpush(s.fields, veclen(output_columns));
    for (size_t i=0; i<veclen(output_columns); i++) {
      s.fields[i].column = output_columns[i];
    }

    vecfree(output_columns);
  }

  analyze_fields(&s);

  struct csv_parser p = {0};
  if (csv_init(&p, 0)) {
    usage();
    exit(1);
  }

  csv_set_delim(&p, delim);

  if (veclen(s.fields) == 0) {
    process_header(f, &s, &p);
    /* printf("Mode: Headers\n"); */
  } else if (s.fields[veclast(s.fields)].quick) {
    process_easymode(f, &s, &p);
    /* printf("Mode: Easy\n"); */
  } else {
    process_fullmode(f, &s, &p);
    /* printf("Mode: Full\n"); */
  }

  csv_free(&p);

  arfree(&a);
  vecfree(s.skip_table);
  vecfree(s.fields);
  
  fclose(f);
  return 0;
}
