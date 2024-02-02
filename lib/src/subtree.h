#ifndef TREE_SITTER_SUBTREE_H_
#define TREE_SITTER_SUBTREE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include "./length.h"
#include "./array.h"
#include "./error_costs.h"
#include "./host.h"
#include "tree_sitter/api.h"
#include "tree_sitter/parser.h"

#define TS_TREE_STATE_NONE USHRT_MAX
#define NULL_SUBTREE ((Subtree) {.ptr = NULL})

// The serialized state of an external scanner.
//
// Every time an external token subtree is created after a call to an
// external scanner, the scanner's `serialize` function is called to
// retrieve a serialized copy of its state. The bytes are then copied
// onto the subtree itself so that the scanner's state can later be
// restored using its `deserialize` function.
//
// Small byte arrays are stored inline, and long ones are allocated
// separately on the heap.
typedef struct {
  union {
    char *long_data;
    char short_data[24];
  };
  uint32_t length;
} ExternalScannerState;

// A compact representation of a subtree.
//
// This representation is used for small leaf nodes that are not
// errors, and were not created by an external scanner.
//
// The idea behind the layout of this struct is that the `is_inline`
// bit will fall exactly into the same location as the least significant
// bit of the pointer in `Subtree` or `MutableSubtree`, respectively.
// Because of alignment, for any valid pointer this will be 0, giving
// us the opportunity to make use of this bit to signify whether to use
// the pointer or the inline struct.
typedef struct SubtreeInlineData SubtreeInlineData;

#define SUBTREE_BITS    \
  bool visible : 1;     \
  bool named : 1;       \
  bool extra : 1;       \
  bool has_changes : 1; \
  bool is_missing : 1;  \
  bool is_keyword : 1;

#define SUBTREE_SIZE           \
  uint8_t padding_columns;     \
  uint8_t padding_rows : 4;    \
  uint8_t lookahead_bytes : 4; \
  uint8_t padding_bytes;       \
  uint8_t size_bytes;

#if TS_BIG_ENDIAN
#if TS_PTR_SIZE == 32

struct SubtreeInlineData {
  uint16_t parse_state;
  uint8_t symbol;
  SUBTREE_BITS
  bool unused : 1;
  bool is_inline : 1;
  SUBTREE_SIZE
};

#else

struct SubtreeInlineData {
  SUBTREE_SIZE
  uint16_t parse_state;
  uint8_t symbol;
  SUBTREE_BITS
  bool unused : 1;
  bool is_inline : 1;
};

#endif
#else

struct SubtreeInlineData {
  bool is_inline : 1;
  SUBTREE_BITS
  uint8_t symbol;
  uint16_t parse_state;
  SUBTREE_SIZE
};

#endif

#undef SUBTREE_BITS
#undef SUBTREE_SIZE

// A heap-allocated representation of a subtree.
//
// This representation is used for parent nodes, external tokens,
// errors, and other leaf nodes whose data is too large to fit into
// the inline representation.
typedef struct {
  volatile uint32_t ref_count;
  Length padding;
  Length size;
  uint32_t lookahead_bytes;
  uint32_t error_cost;
  uint32_t child_count;
  TSSymbol symbol;
  TSStateId parse_state;

  bool visible : 1;
  bool named : 1;
  bool extra : 1;
  bool fragile_left : 1;
  bool fragile_right : 1;
  bool has_changes : 1;
  bool has_external_tokens : 1;
  bool has_external_scanner_state_change : 1;
  bool depends_on_column: 1;
  bool is_missing : 1;
  bool is_keyword : 1;

  union {
    // Non-terminal subtrees (`child_count > 0`)
    struct {
      uint32_t visible_child_count;
      uint32_t named_child_count;
      uint32_t node_count;
      int32_t dynamic_precedence;
      uint16_t repeat_depth;
      uint16_t production_id;
      struct {
        TSSymbol symbol;
        TSStateId parse_state;
      } first_leaf;
    };

    // External terminal subtrees (`child_count == 0 && has_external_tokens`)
    ExternalScannerState external_scanner_state;

    // Error terminal subtrees (`child_count == 0 && symbol == ts_builtin_sym_error`)
    int32_t lookahead_char;
  };
} SubtreeHeapData;

// The fundamental building block of a syntax tree.
typedef union {
  SubtreeInlineData data;
  const SubtreeHeapData *ptr;
} Subtree;

// Like Subtree, but mutable.
typedef union {
  SubtreeInlineData data;
  SubtreeHeapData *ptr;
} MutableSubtree;

typedef Array(Subtree) SubtreeArray;
typedef Array(MutableSubtree) MutableSubtreeArray;

typedef struct {
  MutableSubtreeArray free_trees;
  MutableSubtreeArray tree_stack;
} SubtreePool;

void ts_external_scanner_state_init(ExternalScannerState *, const char *, unsigned);
const char *ts_external_scanner_state_data(const ExternalScannerState *);
bool ts_external_scanner_state_eq(const ExternalScannerState *a, const char *, unsigned);
void ts_external_scanner_state_delete(ExternalScannerState *self);

void ts_subtree_array_copy(SubtreeArray, SubtreeArray *);
void ts_subtree_array_clear(SubtreePool *, SubtreeArray *);
void ts_subtree_array_delete(SubtreePool *, SubtreeArray *);
void ts_subtree_array_remove_trailing_extras(SubtreeArray *, SubtreeArray *);
void ts_subtree_array_reverse(SubtreeArray *);

SubtreePool ts_subtree_pool_new(uint32_t capacity);
void ts_subtree_pool_delete(SubtreePool *);

Subtree ts_subtree_new_leaf(
  SubtreePool *, TSSymbol, Length, Length, uint32_t,
  TSStateId, bool, bool, bool, const TSLanguage *
);
Subtree ts_subtree_new_error(
  SubtreePool *, int32_t, Length, Length, uint32_t, TSStateId, const TSLanguage *
);
MutableSubtree ts_subtree_new_node(TSSymbol, SubtreeArray *, unsigned, const TSLanguage *);
Subtree ts_subtree_new_error_node(SubtreeArray *, bool, const TSLanguage *);
Subtree ts_subtree_new_missing_leaf(SubtreePool *, TSSymbol, Length, uint32_t, const TSLanguage *);
MutableSubtree ts_subtree_make_mut(SubtreePool *, Subtree);
void ts_subtree_retain(Subtree);
void ts_subtree_release(SubtreePool *, Subtree);
uint64_t ts_subtree_sizeof(SubtreePool *, Subtree *);
int ts_subtree_compare(Subtree, Subtree);
void ts_subtree_set_symbol(MutableSubtree *, TSSymbol, const TSLanguage *);
void ts_subtree_summarize(MutableSubtree, const Subtree *, uint32_t, const TSLanguage *);
void ts_subtree_summarize_children(MutableSubtree, const TSLanguage *);
void ts_subtree_balance(Subtree, SubtreePool *, const TSLanguage *);
Subtree ts_subtree_edit(Subtree, const TSInputEdit *edit, SubtreePool *);
char *ts_subtree_string(Subtree, const TSLanguage *, bool include_all);
void ts_subtree_print_dot_graph(Subtree, const TSLanguage *, FILE *);
Subtree ts_subtree_last_external_token(Subtree);
const ExternalScannerState *ts_subtree_external_scanner_state(Subtree self);
bool ts_subtree_external_scanner_state_eq(Subtree, Subtree);

#define SUBTREE_GET(self, name) (self.data.is_inline ? self.data.name : self.ptr->name)

static inline TSSymbol ts_subtree_symbol(Subtree self) { return SUBTREE_GET(self, symbol); }
static inline bool ts_subtree_visible(Subtree self) { return SUBTREE_GET(self, visible); }
static inline bool ts_subtree_named(Subtree self) { return SUBTREE_GET(self, named); }
static inline bool ts_subtree_extra(Subtree self) { return SUBTREE_GET(self, extra); }
static inline bool ts_subtree_has_changes(Subtree self) { return SUBTREE_GET(self, has_changes); }
static inline bool ts_subtree_missing(Subtree self) { return SUBTREE_GET(self, is_missing); }
static inline bool ts_subtree_is_keyword(Subtree self) { return SUBTREE_GET(self, is_keyword); }
static inline TSStateId ts_subtree_parse_state(Subtree self) { return SUBTREE_GET(self, parse_state); }
static inline uint32_t ts_subtree_lookahead_bytes(Subtree self) { return SUBTREE_GET(self, lookahead_bytes); }

#undef SUBTREE_GET

// Get the size needed to store a heap-allocated subtree with the given
// number of children.
static inline size_t ts_subtree_alloc_size(uint32_t child_count) {
  return child_count * sizeof(Subtree) + sizeof(SubtreeHeapData);
}

// Get a subtree's children, which are allocated immediately before the
// tree's own heap data.
#define ts_subtree_children(self) \
  ((self).data.is_inline ? NULL : (Subtree *)((self).ptr) - (self).ptr->child_count)

static inline void ts_subtree_set_extra(MutableSubtree *self, bool is_extra) {
  if (self->data.is_inline) {
    self->data.extra = is_extra;
  } else {
    self->ptr->extra = is_extra;
  }
}

static inline TSSymbol ts_subtree_leaf_symbol(Subtree self) {
  if (self.data.is_inline) return self.data.symbol;
  if (self.ptr->child_count == 0) return self.ptr->symbol;
  return self.ptr->first_leaf.symbol;
}

static inline TSStateId ts_subtree_leaf_parse_state(Subtree self) {
  if (self.data.is_inline) return self.data.parse_state;
  if (self.ptr->child_count == 0) return self.ptr->parse_state;
  return self.ptr->first_leaf.parse_state;
}

static inline Length ts_subtree_padding(Subtree self) {
  if (self.data.is_inline) {
    Length result = {self.data.padding_bytes, {self.data.padding_rows, self.data.padding_columns}};
    return result;
  } else {
    return self.ptr->padding;
  }
}

static inline Length ts_subtree_size(Subtree self) {
  if (self.data.is_inline) {
    Length result = {self.data.size_bytes, {0, self.data.size_bytes}};
    return result;
  } else {
    return self.ptr->size;
  }
}

static inline Length ts_subtree_total_size(Subtree self) {
  return length_add(ts_subtree_padding(self), ts_subtree_size(self));
}

static inline uint32_t ts_subtree_total_bytes(Subtree self) {
  return ts_subtree_total_size(self).bytes;
}

static inline uint32_t ts_subtree_child_count(Subtree self) {
  return self.data.is_inline ? 0 : self.ptr->child_count;
}

static inline uint32_t ts_subtree_repeat_depth(Subtree self) {
  return self.data.is_inline ? 0 : self.ptr->repeat_depth;
}

static inline uint32_t ts_subtree_is_repetition(Subtree self) {
  return self.data.is_inline
    ? 0
    : !self.ptr->named && !self.ptr->visible && self.ptr->child_count != 0;
}

static inline uint32_t ts_subtree_node_count(Subtree self) {
  return (self.data.is_inline || self.ptr->child_count == 0) ? 1 : self.ptr->node_count;
}

static inline uint32_t ts_subtree_visible_child_count(Subtree self) {
  if (ts_subtree_child_count(self) > 0) {
    return self.ptr->visible_child_count;
  } else {
    return 0;
  }
}

static inline uint32_t ts_subtree_error_cost(Subtree self) {
  if (ts_subtree_missing(self)) {
    return ERROR_COST_PER_MISSING_TREE + ERROR_COST_PER_RECOVERY;
  } else {
    return self.data.is_inline ? 0 : self.ptr->error_cost;
  }
}

static inline int32_t ts_subtree_dynamic_precedence(Subtree self) {
  return (self.data.is_inline || self.ptr->child_count == 0) ? 0 : self.ptr->dynamic_precedence;
}

static inline uint16_t ts_subtree_production_id(Subtree self) {
  if (ts_subtree_child_count(self) > 0) {
    return self.ptr->production_id;
  } else {
    return 0;
  }
}

static inline bool ts_subtree_fragile_left(Subtree self) {
  return self.data.is_inline ? false : self.ptr->fragile_left;
}

static inline bool ts_subtree_fragile_right(Subtree self) {
  return self.data.is_inline ? false : self.ptr->fragile_right;
}

static inline bool ts_subtree_has_external_tokens(Subtree self) {
  return self.data.is_inline ? false : self.ptr->has_external_tokens;
}

static inline bool ts_subtree_has_external_scanner_state_change(Subtree self) {
  return self.data.is_inline ? false : self.ptr->has_external_scanner_state_change;
}

static inline bool ts_subtree_depends_on_column(Subtree self) {
  return self.data.is_inline ? false : self.ptr->depends_on_column;
}

static inline bool ts_subtree_is_fragile(Subtree self) {
  return self.data.is_inline ? false : (self.ptr->fragile_left || self.ptr->fragile_right);
}

static inline bool ts_subtree_is_error(Subtree self) {
  return ts_subtree_symbol(self) == ts_builtin_sym_error;
}

static inline bool ts_subtree_is_eof(Subtree self) {
  return ts_subtree_symbol(self) == ts_builtin_sym_end;
}

static inline Subtree ts_subtree_from_mut(MutableSubtree self) {
  Subtree result;
  result.data = self.data;
  return result;
}

static inline MutableSubtree ts_subtree_to_mut_unsafe(Subtree self) {
  MutableSubtree result;
  result.data = self.data;
  return result;
}

#ifdef __cplusplus
}
#endif

#endif  // TREE_SITTER_SUBTREE_H_
