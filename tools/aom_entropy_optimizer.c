/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

// This tool is a gadget for offline probability training.
// A binary executable aom_entropy_optimizer will be generated in tools/. It
// parses a binary file consisting of counts written in the format of
// FRAME_COUNTS in entropymode.h, and computes optimized probability tables
// and CDF tables, which will be written to a new c file optimized_probs.c
// according to format in the codebase.
//
// Command line: ./aom_entropy_optimizer [directory of the count file]
//
// The input file can either be generated by encoding a single clip by
// turning on entropy_stats experiment, or be collected at a larger scale at
// which a python script which will be provided soon can be used to aggregate
// multiple stats output.

#include <assert.h>
#include <stdio.h>
#include "./aom_config.h"
#include "av1/common/entropymode.h"

#if CONFIG_SMOOTH_HV
const aom_tree_index av1_intra_mode_tree[TREE_SIZE(INTRA_MODES)] = {
  -DC_PRED,
  2, /* 0 = DC_NODE */
  -TM_PRED,
  4, /* 1 = TM_NODE */
  -V_PRED,
  6, /* 2 = V_NODE */
  8,
  12, /* 3 = COM_NODE */
  -H_PRED,
  10, /* 4 = H_NODE */
  -D135_PRED,
  -D117_PRED, /* 5 = D135_NODE */
  -D45_PRED,
  14, /* 6 = D45_NODE */
  -D63_PRED,
  16, /* 7 = D63_NODE */
  -D153_PRED,
  18, /* 8 = D153_NODE */
  -D207_PRED,
  20, /* 9 = D207_NODE */
  -SMOOTH_PRED,
  22, /* 10 = SMOOTH_NODE */
  -SMOOTH_V_PRED,
  -SMOOTH_H_PRED /* 11 = SMOOTH_V_NODE */
};
#else
const aom_tree_index av1_intra_mode_tree[TREE_SIZE(INTRA_MODES)] = {
  -DC_PRED,   2,            /* 0 = DC_NODE */
  -TM_PRED,   4,            /* 1 = TM_NODE */
  -V_PRED,    6,            /* 2 = V_NODE */
  8,          12,           /* 3 = COM_NODE */
  -H_PRED,    10,           /* 4 = H_NODE */
  -D135_PRED, -D117_PRED,   /* 5 = D135_NODE */
  -D45_PRED,  14,           /* 6 = D45_NODE */
  -D63_PRED,  16,           /* 7 = D63_NODE */
  -D153_PRED, 18,           /* 8 = D153_NODE */
  -D207_PRED, -SMOOTH_PRED, /* 9 = D207_NODE */
};
#endif  // CONFIG_SMOOTH_HV

#define SPACES_PER_TAB 2

typedef unsigned int aom_count_type;
// A log file recording parsed counts
static FILE *logfile;  // TODO(yuec): make it a command line option

// Optimized probabilities will be stored in probs[].
static unsigned int optimize_tree_probs(const aom_tree_index *tree,
                                        unsigned int idx,
                                        const unsigned int *counts,
                                        aom_prob *probs) {
  const int l = tree[idx];
  const unsigned int left_count =
      (l <= 0) ? counts[-l] : optimize_tree_probs(tree, l, counts, probs);
  const int r = tree[idx + 1];
  const unsigned int right_count =
      (r <= 0) ? counts[-r] : optimize_tree_probs(tree, r, counts, probs);
  probs[idx >> 1] = get_binary_prob(left_count, right_count);
  return left_count + right_count;
}

static int parse_stats(aom_count_type **ct_ptr, FILE *const probsfile, int tabs,
                       int dim_of_cts, int *cts_each_dim,
                       const aom_tree_index *tree, int flatten_last_dim) {
  if (dim_of_cts < 1) {
    fprintf(stderr, "The dimension of a counts vector should be at least 1!\n");
    return 1;
  }
  if (dim_of_cts == 1) {
    const int total_modes = cts_each_dim[0];
    aom_count_type *counts1d = *ct_ptr;
    aom_prob *probs = aom_malloc(sizeof(*probs) * (total_modes - 1));

    if (probs == NULL) {
      fprintf(stderr, "Allocating prob array failed!\n");
      return 1;
    }

    (*ct_ptr) += total_modes;
    if (tree != NULL) {
      optimize_tree_probs(tree, 0, counts1d, probs);
    } else {
      assert(total_modes == 2);
      probs[0] = get_binary_prob(counts1d[0], counts1d[1]);
    }
    if (tabs > 0) fprintf(probsfile, "%*c", tabs * SPACES_PER_TAB, ' ');
    for (int k = 0; k < total_modes - 1; ++k) {
      if (k == total_modes - 2)
        fprintf(probsfile, " %3d ", probs[k]);
      else
        fprintf(probsfile, " %3d,", probs[k]);
      fprintf(logfile, "%d ", counts1d[k]);
    }
    fprintf(logfile, "%d\n", counts1d[total_modes - 1]);
  } else if (dim_of_cts == 2 && flatten_last_dim) {
    assert(cts_each_dim[1] == 2);

    for (int k = 0; k < cts_each_dim[0]; ++k) {
      if (k == cts_each_dim[0] - 1) {
        fprintf(probsfile, " %3d ",
                get_binary_prob((*ct_ptr)[0], (*ct_ptr)[1]));
      } else {
        fprintf(probsfile, " %3d,",
                get_binary_prob((*ct_ptr)[0], (*ct_ptr)[1]));
      }
      fprintf(logfile, "%d %d\n", (*ct_ptr)[0], (*ct_ptr)[1]);
      (*ct_ptr) += 2;
    }
  } else {
    for (int k = 0; k < cts_each_dim[0]; ++k) {
      int tabs_next_level;
      if (dim_of_cts == 2 || (dim_of_cts == 3 && flatten_last_dim)) {
        fprintf(probsfile, "%*c{", tabs * SPACES_PER_TAB, ' ');
        tabs_next_level = 0;
      } else {
        fprintf(probsfile, "%*c{\n", tabs * SPACES_PER_TAB, ' ');
        tabs_next_level = tabs + 1;
      }
      if (parse_stats(ct_ptr, probsfile, tabs_next_level, dim_of_cts - 1,
                      cts_each_dim + 1, tree, flatten_last_dim)) {
        return 1;
      }
      if (dim_of_cts == 2 || (dim_of_cts == 3 && flatten_last_dim)) {
        if (k == cts_each_dim[0] - 1)
          fprintf(probsfile, "}\n");
        else
          fprintf(probsfile, "},\n");
      } else {
        if (k == cts_each_dim[0] - 1)
          fprintf(probsfile, "%*c}\n", tabs * SPACES_PER_TAB, ' ');
        else
          fprintf(probsfile, "%*c},\n", tabs * SPACES_PER_TAB, ' ');
      }
    }
  }
  return 0;
}

// This function parses the stats of a syntax, either binary or multi-symbol,
// in different contexts, and writes the optimized probability table to
// probsfile.
//   counts: pointer of the first count element in counts array
//   probsfile: output file
//   dim_of_cts: number of dimensions of counts array
//   cts_each_dim: an array storing size of each dimension of counts array
//   tree: binary tree for a multi-symbol syntax, or NULL for a binary one
//   flatten_last_dim: for a binary syntax, if flatten_last_dim is 0, probs in
//                     different contexts will be written separately, e.g.,
//                     {{p1}, {p2}, ...};
//                     otherwise will be grouped together at the second last
//                     dimension, i.e.,
//                     {p1, p2, ...}.
//   prefix: declaration header for the entropy table
static void optimize_entropy_table(aom_count_type *counts,
                                   FILE *const probsfile, int dim_of_cts,
                                   int *cts_each_dim,
                                   const aom_tree_index *tree,
                                   int flatten_last_dim, char *prefix) {
  aom_count_type *ct_ptr = counts;

  assert(!flatten_last_dim || cts_each_dim[dim_of_cts - 1] == 2);

  fprintf(probsfile, "%s = {\n", prefix);
  if (parse_stats(&ct_ptr, probsfile, 1, dim_of_cts, cts_each_dim, tree,
                  flatten_last_dim)) {
    fprintf(probsfile, "Optimizer failed!\n");
  }
  fprintf(probsfile, "};\n\n");
  fprintf(logfile, "\n");
}

static int counts_to_cdf(const aom_count_type *counts, aom_cdf_prob *cdf,
                         int modes) {
  int64_t *csum = aom_malloc(sizeof(*csum) * modes);

  if (csum == NULL) {
    fprintf(stderr, "Allocating csum array failed!\n");
    return 1;
  }
  csum[0] = counts[0];
  for (int i = 1; i < modes; ++i) csum[i] = counts[i] + csum[i - 1];

  int64_t sum = csum[modes - 1];
  int64_t round_shift = sum >> 1;
  for (int i = 0; i < modes; ++i) {
    if (sum <= 0)
      cdf[i] = CDF_PROB_TOP;
    else
      cdf[i] = (csum[i] * CDF_PROB_TOP + round_shift) / sum;
  }
  return 0;
}

static int parse_counts_for_cdf_opt(aom_count_type **ct_ptr,
                                    FILE *const probsfile, int tabs,
                                    int dim_of_cts, int *cts_each_dim) {
  if (dim_of_cts < 1) {
    fprintf(stderr, "The dimension of a counts vector should be at least 1!\n");
    return 1;
  }
  if (dim_of_cts == 1) {
    const int total_modes = cts_each_dim[0];
    aom_count_type *counts1d = *ct_ptr;
    aom_cdf_prob *cdfs = aom_malloc(sizeof(*cdfs) * total_modes);

    if (cdfs == NULL) {
      fprintf(stderr, "Allocating cdf array failed!\n");
      return 1;
    }

    counts_to_cdf(counts1d, cdfs, total_modes);
    (*ct_ptr) += total_modes;

    if (tabs > 0) fprintf(probsfile, "%*c", tabs * SPACES_PER_TAB, ' ');
    for (int k = 0; k < total_modes; ++k)
      fprintf(probsfile, " AOM_ICDF(%d),", cdfs[k]);
    fprintf(probsfile, " 0 ");
  } else {
    for (int k = 0; k < cts_each_dim[0]; ++k) {
      int tabs_next_level;

      if (dim_of_cts == 2)
        fprintf(probsfile, "%*c{", tabs * SPACES_PER_TAB, ' ');
      else
        fprintf(probsfile, "%*c{\n", tabs * SPACES_PER_TAB, ' ');
      tabs_next_level = dim_of_cts == 2 ? 0 : tabs + 1;

      if (parse_counts_for_cdf_opt(ct_ptr, probsfile, tabs_next_level,
                                   dim_of_cts - 1, cts_each_dim + 1)) {
        return 1;
      }

      if (dim_of_cts == 2) {
        if (k == cts_each_dim[0] - 1)
          fprintf(probsfile, "}\n");
        else
          fprintf(probsfile, "},\n");
      } else {
        if (k == cts_each_dim[0] - 1)
          fprintf(probsfile, "%*c}\n", tabs * SPACES_PER_TAB, ' ');
        else
          fprintf(probsfile, "%*c},\n", tabs * SPACES_PER_TAB, ' ');
      }
    }
  }

  return 0;
}

static void optimize_cdf_table(aom_count_type *counts, FILE *const probsfile,
                               int dim_of_cts, int *cts_each_dim,
                               char *prefix) {
  aom_count_type *ct_ptr = counts;

  fprintf(probsfile, "%s = {\n", prefix);
  if (parse_counts_for_cdf_opt(&ct_ptr, probsfile, 1, dim_of_cts,
                               cts_each_dim)) {
    fprintf(probsfile, "Optimizer failed!\n");
  }
  fprintf(probsfile, "};\n\n");
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Please specify the input stats file!\n");
    exit(EXIT_FAILURE);
  }

  FILE *const statsfile = fopen(argv[1], "rb");
  if (statsfile == NULL) {
    fprintf(stderr, "Failed to open input file!\n");
    exit(EXIT_FAILURE);
  }

  FRAME_COUNTS fc;
  fread(&fc, sizeof(FRAME_COUNTS), 1, statsfile);

  FILE *const probsfile = fopen("optimized_probs.c", "w");
  if (probsfile == NULL) {
    fprintf(stderr,
            "Failed to create output file for optimized entropy tables!\n");
    exit(EXIT_FAILURE);
  }

  logfile = fopen("aom_entropy_optimizer_parsed_counts.log", "w");
  if (logfile == NULL) {
    fprintf(stderr, "Failed to create log file for parsed counts!\n");
    exit(EXIT_FAILURE);
  }

  int cts_each_dim[10];

  /* Intra mode (keyframe luma) */
  cts_each_dim[0] = INTRA_MODES;
  cts_each_dim[1] = INTRA_MODES;
  cts_each_dim[2] = INTRA_MODES;
  optimize_entropy_table(
      &fc.kf_y_mode[0][0][0], probsfile, 3, cts_each_dim, av1_intra_mode_tree,
      0,
      "const aom_prob av1_kf_y_mode_prob[INTRA_MODES][INTRA_MODES]"
      "[INTRA_MODES - 1]");
  optimize_cdf_table(
      &fc.kf_y_mode[0][0][0], probsfile, 3, cts_each_dim,
      "const aom_cdf_prob\n"
      "av1_kf_y_mode_cdf[INTRA_MODES][INTRA_MODES][CDF_SIZE(INTRA_MODES)]");

  /* Intra mode (non-keyframe luma) */
  cts_each_dim[0] = BLOCK_SIZE_GROUPS;
  cts_each_dim[1] = INTRA_MODES;
  optimize_entropy_table(
      &fc.y_mode[0][0], probsfile, 2, cts_each_dim, av1_intra_mode_tree, 0,
      "static const aom_prob default_if_y_probs[BLOCK_SIZE_GROUPS]"
      "[INTRA_MODES - 1]");
  optimize_cdf_table(
      &fc.y_mode[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_if_y_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(INTRA_MODES)]");

  /* Intra mode (chroma) */
  cts_each_dim[0] = INTRA_MODES;
  cts_each_dim[1] = UV_INTRA_MODES;
  optimize_entropy_table(&fc.uv_mode[0][0], probsfile, 2, cts_each_dim,
                         av1_intra_mode_tree, 0,
                         "static const aom_prob default_uv_probs[INTRA_MODES]"
                         "[UV_INTRA_MODES - 1]");
  optimize_cdf_table(
      &fc.uv_mode[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_uv_mode_cdf[INTRA_MODES][CDF_SIZE(UV_INTRA_MODES)]");

  /* Partition */
  cts_each_dim[0] = PARTITION_CONTEXTS;
#if CONFIG_EXT_PARTITION_TYPES
  cts_each_dim[1] = EXT_PARTITION_TYPES;
  // TODO(yuec): Wrong prob for context = 0, because the old tree is used
  optimize_entropy_table(&fc.partition[0][0], probsfile, 2, cts_each_dim,
                         av1_ext_partition_tree, 0,
                         "static const aom_prob default_partition_probs"
                         "[PARTITION_CONTEXTS][EXT_PARTITION_TYPES - 1]");
  optimize_cdf_table(&fc.partition[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_partition_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_"
                     "PARTITION_TYPES)]");
#else
  cts_each_dim[1] = PARTITION_TYPES;
  optimize_entropy_table(&fc.partition[0][0], probsfile, 2, cts_each_dim,
                         av1_partition_tree, 0,
                         "static const aom_prob default_partition_probs"
                         "[PARTITION_CONTEXTS][PARTITION_TYPES - 1]");
  optimize_cdf_table(
      &fc.partition[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_partition_cdf[PARTITION_CONTEXTS][CDF_SIZE(PARTITION_TYPES)]");
#endif

  /* Interpolation filter */
  cts_each_dim[0] = SWITCHABLE_FILTER_CONTEXTS;
  cts_each_dim[1] = SWITCHABLE_FILTERS;
  optimize_entropy_table(
      &fc.switchable_interp[0][0], probsfile, 2, cts_each_dim,
      av1_switchable_interp_tree, 0,
      "static const aom_prob \n"
      "default_switchable_interp_prob[SWITCHABLE_FILTER_CONTEXTS]"
      "[SWITCHABLE_FILTERS - 1]");
  optimize_cdf_table(&fc.switchable_interp[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS]"
                     "[CDF_SIZE(SWITCHABLE_FILTERS)]");

  /* Blockzero */
  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = REF_TYPES;
  cts_each_dim[3] = BLOCKZ_CONTEXTS;
  cts_each_dim[4] = 2;
  optimize_entropy_table(
      &fc.blockz_count[0][0][0][0][0], probsfile, 5, cts_each_dim, NULL, 1,
      "static const aom_prob av1_default_blockzero_probs[TX_SIZES]"
      "[PLANE_TYPES][REF_TYPES][BLOCKZ_CONTEXTS]");

  /* Motion vector referencing */
  cts_each_dim[0] = NEWMV_MODE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.newmv_mode[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_newmv_prob[NEWMV_MODE_CONTEXTS]");
  optimize_cdf_table(&fc.newmv_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_newmv_cdf[NEWMV_MODE_CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = ZEROMV_MODE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.zeromv_mode[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_zeromv_prob[ZEROMV_MODE_CONTEXTS]");
  optimize_cdf_table(&fc.zeromv_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_zeromv_cdf[ZEROMV_MODE_CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = REFMV_MODE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.refmv_mode[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_refmv_prob[REFMV_MODE_CONTEXTS]");
  optimize_cdf_table(&fc.refmv_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_refmv_cdf[REFMV_MODE_CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = DRL_MODE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.drl_mode[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_drl_prob[DRL_MODE_CONTEXTS]");
  optimize_cdf_table(&fc.drl_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_drl_cdf[DRL_MODE_CONTEXTS][CDF_SIZE(2)]");

/* ext_inter experiment */
#if CONFIG_EXT_INTER
  /* New compound mode */
  cts_each_dim[0] = INTER_MODE_CONTEXTS;
  cts_each_dim[1] = INTER_COMPOUND_MODES;
  optimize_entropy_table(
      &fc.inter_compound_mode[0][0], probsfile, 2, cts_each_dim,
      av1_inter_compound_mode_tree, 0,
      "static const aom_prob default_inter_compound_mode_probs\n"
      "[INTER_MODE_CONTEXTS][INTER_COMPOUND_MODES - 1]");
  optimize_cdf_table(&fc.inter_compound_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_inter_compound_mode_cdf[INTER_MODE_CONTEXTS][CDF_"
                     "SIZE(INTER_COMPOUND_MODES)]");
#if CONFIG_COMPOUND_SINGLEREF
  /* Compound singleref mode */
  cts_each_dim[0] = INTER_MODE_CONTEXTS;
  cts_each_dim[1] = INTER_SINGLEREF_COMP_MODES;
  optimize_entropy_table(
      &fc.inter_singleref_comp_mode[0][0], probsfile, 2, cts_each_dim,
      av1_inter_singleref_comp_mode_tree, 0,
      "static const aom_prob default_inter_singleref_comp_mode_probs\n"
      "[INTER_MODE_CONTEXTS][INTER_SINGLEREF_COMP_MODES - 1]");
  optimize_cdf_table(&fc.inter_singleref_comp_mode[0][0], probsfile, 2,
                     cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_inter_singleref_comp_mode_cdf[INTER_MODE_"
                     "CONTEXTS][CDF_SIZE(INTER_SINGLEREF_COMP_MODES)]");
#endif
#if CONFIG_INTERINTRA
  /* Interintra */
  cts_each_dim[0] = BLOCK_SIZE_GROUPS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.interintra[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_interintra_prob[BLOCK_SIZE_GROUPS]");
  optimize_cdf_table(&fc.interintra[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)]");

  cts_each_dim[0] = BLOCK_SIZE_GROUPS;
  cts_each_dim[1] = INTERINTRA_MODES;
  optimize_entropy_table(
      &fc.interintra_mode[0][0], probsfile, 2, cts_each_dim,
      av1_interintra_mode_tree, 0,
      "static const aom_prob "
      "default_interintra_mode_prob[BLOCK_SIZE_GROUPS][INTERINTRA_MODES - 1]");
  optimize_cdf_table(&fc.interintra_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_interintra_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE("
                     "INTERINTRA_MODES)]");

  cts_each_dim[0] = BLOCK_SIZES_ALL;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.wedge_interintra[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_wedge_interintra_prob[BLOCK_SIZES_ALL]");
  optimize_cdf_table(
      &fc.wedge_interintra[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_wedge_interintra_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)]");
#endif
  /* Compound type */
  cts_each_dim[0] = BLOCK_SIZES_ALL;
  cts_each_dim[1] = COMPOUND_TYPES;
  optimize_entropy_table(&fc.compound_interinter[0][0], probsfile, 2,
                         cts_each_dim, av1_compound_type_tree, 0,
                         "static const aom_prob default_compound_type_probs"
                         "[BLOCK_SIZES_ALL][COMPOUND_TYPES - 1]");
  optimize_cdf_table(
      &fc.compound_interinter[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_compound_type_cdf[BLOCK_SIZES_ALL][CDF_SIZE(COMPOUND_TYPES)]");
#endif

/* motion_var and warped_motion experiments */
#if CONFIG_MOTION_VAR || CONFIG_WARPED_MOTION
  cts_each_dim[0] = BLOCK_SIZES_ALL;
  cts_each_dim[1] = MOTION_MODES;
  optimize_entropy_table(
      &fc.motion_mode[0][0], probsfile, 2, cts_each_dim, av1_motion_mode_tree,
      0,
      "static const aom_prob default_motion_mode_prob[BLOCK_SIZES]"
      "[MOTION_MODES - 1]");
  optimize_cdf_table(
      &fc.motion_mode[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_motion_mode_cdf[BLOCK_SIZES_ALL][CDF_SIZE(MOTION_MODES)]");
#if CONFIG_MOTION_VAR && CONFIG_WARPED_MOTION
  cts_each_dim[0] = BLOCK_SIZES_ALL;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.obmc[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_obmc_prob[BLOCK_SIZES_ALL]");
  optimize_cdf_table(&fc.obmc[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_obmc_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)]");
#endif  // CONFIG_MOTION_VAR && CONFIG_WARPED_MOTION
#if CONFIG_NCOBMC_ADAPT_WEIGHT
  cts_each_dim[0] = ADAPT_OVERLAP_BLOCKS;
  cts_each_dim[1] = MAX_NCOBMC_MODES;
  optimize_entropy_table(
      &fc.ncobmc_mode[0][0], probsfile, 2, cts_each_dim, av1_ncobmc_mode_tree,
      0,
      "static const aom_prob default_ncobmc_mode_prob[ADAPT_OVERLAP_BLOCKS]"
      "[MAX_NCOBMC_MODES - 1]");
  optimize_cdf_table(&fc.ncobmc_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_ncobmc_mode_cdf[ADAPT_OVERLAP_BLOCKS]"
                     "[CDF_SIZE(MAX_NCOBMC_MODES)]");
#if CONFIG_WARPED_MOTION
  cts_each_dim[0] = BLOCK_SIZES_ALL;
  cts_each_dim[1] = OBMC_FAMILY_MODES;
  optimize_entropy_table(
      &fc.ncobmc[0][0], probsfile, 2, cts_each_dim, av1_ncobmc_tree, 0,
      "static const aom_prob default_ncobmc_prob[BLOCK_SIZES_ALL]"
      "[OBMC_FAMILY_MODES - 1]");
  optimize_cdf_table(&fc.ncobmc[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_ncobmc_cdf[BLOCK_SIZES_ALL]"
                     "[CDF_SIZE(OBMC_FAMILY_MODES)]");
#endif
#endif  // CONFIG_NCOBMC_ADAPT_WEIGHT
#endif  // CONFIG_MOTION_VAR || CONFIG_WARPED_MOTION

  /* Intra/inter flag */
  cts_each_dim[0] = INTRA_INTER_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(&fc.intra_inter[0][0], probsfile, 2, cts_each_dim,
                         NULL, 1,
                         "static const aom_prob default_intra_inter_p"
                         "[INTRA_INTER_CONTEXTS]");
  optimize_cdf_table(
      &fc.intra_inter[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_intra_inter_cdf[INTRA_INTER_CONTEXTS][CDF_SIZE(2)]");

  /* Single/comp ref flag */
  cts_each_dim[0] = COMP_INTER_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(&fc.comp_inter[0][0], probsfile, 2, cts_each_dim, NULL,
                         1,
                         "static const aom_prob default_comp_inter_p"
                         "[COMP_INTER_CONTEXTS]");
  optimize_cdf_table(
      &fc.comp_inter[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)]");

/* ext_comp_refs experiment */
#if CONFIG_EXT_COMP_REFS
  cts_each_dim[0] = COMP_REF_TYPE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.comp_ref_type[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_comp_ref_type_p[COMP_REF_TYPE_CONTEXTS]");
  optimize_cdf_table(
      &fc.comp_ref_type[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_comp_ref_type_cdf[COMP_REF_TYPE_CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = UNI_COMP_REF_CONTEXTS;
  cts_each_dim[1] = UNIDIR_COMP_REFS - 1;
  cts_each_dim[2] = 2;
  optimize_entropy_table(
      &fc.uni_comp_ref[0][0][0], probsfile, 3, cts_each_dim, NULL, 1,
      "static const aom_prob\n"
      "default_uni_comp_ref_p[UNI_COMP_REF_CONTEXTS][UNIDIR_COMP_REFS - 1]");
  optimize_cdf_table(&fc.uni_comp_ref[0][0][0], probsfile, 3, cts_each_dim,
                     "static const aom_cdf_prob\n"
                     "default_uni_comp_ref_cdf[UNI_COMP_REF_CONTEXTS][UNIDIR_"
                     "COMP_REFS - 1][CDF_SIZE(2)]");
#endif

  /* Reference frame (single ref) */
  cts_each_dim[0] = REF_CONTEXTS;
  cts_each_dim[1] = SINGLE_REFS - 1;
  cts_each_dim[2] = 2;
  optimize_entropy_table(
      &fc.single_ref[0][0][0], probsfile, 3, cts_each_dim, NULL, 1,
      "static const aom_prob default_single_ref_p[REF_CONTEXTS]"
      "[SINGLE_REFS - 1]");
  optimize_cdf_table(
      &fc.single_ref[0][0][0], probsfile, 3, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_single_ref_cdf[REF_CONTEXTS][SINGLE_REFS - 1][CDF_SIZE(2)]");

#if CONFIG_EXT_REFS
  /* ext_refs experiment */
  cts_each_dim[0] = REF_CONTEXTS;
  cts_each_dim[1] = FWD_REFS - 1;
  cts_each_dim[2] = 2;
  optimize_entropy_table(
      &fc.comp_ref[0][0][0], probsfile, 3, cts_each_dim, NULL, 1,
      "static const aom_prob default_comp_ref_p[REF_CONTEXTS][FWD_REFS - 1]");
  optimize_cdf_table(
      &fc.comp_ref[0][0][0], probsfile, 3, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_comp_ref_cdf[REF_CONTEXTS][FWD_REFS - 1][CDF_SIZE(2)]");

  cts_each_dim[0] = REF_CONTEXTS;
  cts_each_dim[1] = BWD_REFS - 1;
  cts_each_dim[2] = 2;
  optimize_entropy_table(&fc.comp_bwdref[0][0][0], probsfile, 3, cts_each_dim,
                         NULL, 1,
                         "static const aom_prob "
                         "default_comp_bwdref_p[REF_CONTEXTS][BWD_REFS - 1]");
  optimize_cdf_table(
      &fc.comp_bwdref[0][0][0], probsfile, 3, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_comp_bwdref_cdf[REF_CONTEXTS][BWD_REFS - 1][CDF_SIZE(2)]");
#else
  /* Reference frame (compound refs) */
  cts_each_dim[0] = REF_CONTEXTS;
  cts_each_dim[1] = COMP_REFS - 1;
  cts_each_dim[2] = 2;
  optimize_entropy_table(
      &fc.comp_ref[0][0][0], probsfile, 3, cts_each_dim, NULL, 1,
      "static const aom_prob default_comp_ref_p[REF_CONTEXTS]"
      "[COMP_REFS - 1]");
  optimize_cdf_table(
      &fc.comp_ref[0][0][0], probsfile, 3, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_comp_ref_cdf[REF_CONTEXTS][COMP_REFS - 1][CDF_SIZE(2)]");
#endif  // CONFIG_EXT_REFS

/* Compound single ref inter mode */
#if CONFIG_EXT_INTER && CONFIG_COMPOUND_SINGLEREF
  cts_each_dim[0] = COMP_INTER_MODE_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(&fc.comp_inter_mode[0][0], probsfile, 2, cts_each_dim,
                         NULL, 1,
                         "static const aom_prob "
                         "default_comp_inter_mode_p[COMP_INTER_MODE_CONTEXTS]");
  optimize_cdf_table(&fc.comp_inter_mode[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_comp_inter_mode_cdf[COMP_INTER_MODE_CONTEXTS]["
                     "CDF_SIZE(2)]");
#endif

/* Transform size */
// TODO(yuec): av1_tx_size_tree has variable sizes, so needs special handling
#if CONFIG_RECT_TX_EXT && (CONFIG_EXT_TX || CONFIG_VAR_TX)
  cts_each_dim[0] = 2;
  optimize_entropy_table(&fc.quarter_tx_size[0], probsfile, 1, cts_each_dim,
                         NULL, 1,
                         "static const aom_prob default_quarter_tx_size_prob");
  optimize_cdf_table(
      &fc.quarter_tx_size[0], probsfile, 1, cts_each_dim,
      "static const aom_cdf_prob default_quarter_tx_size_cdf[CDF_SIZE(2)]");
#endif
#if CONFIG_VAR_TX
  cts_each_dim[0] = TXFM_PARTITION_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.txfm_partition[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob "
      "default_txfm_partition_probs[TXFM_PARTITION_CONTEXTS]");
  optimize_cdf_table(
      &fc.txfm_partition[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob\n"
      "default_txfm_partition_cdf[TXFM_PARTITION_CONTEXTS][CDF_SIZE(2)]");
#endif

  /* Skip flag */
  cts_each_dim[0] = SKIP_CONTEXTS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.skip[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_skip_probs[SKIP_CONTEXTS]");
  optimize_cdf_table(&fc.skip[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_skip_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)]");

/* intrabc experiment */
#if CONFIG_INTRABC
  cts_each_dim[0] = 2;
  optimize_entropy_table(&fc.intrabc[0], probsfile, 1, cts_each_dim, NULL, 1,
                         "INTRABC_PROB_DEFAULT");
  optimize_cdf_table(
      &fc.intrabc[0], probsfile, 1, cts_each_dim,
      "static const aom_cdf_prob default_intrabc_cdf[CDF_SIZE(2)]");
#endif

/* delta_q experiment */
#if CONFIG_DELTA_Q
  cts_each_dim[0] = DELTA_Q_PROBS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.delta_q[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_delta_q_probs[DELTA_Q_PROBS]");
#if CONFIG_EXT_DELTA_Q
  cts_each_dim[0] = DELTA_LF_PROBS;
  cts_each_dim[1] = 2;
  optimize_entropy_table(
      &fc.delta_lf[0][0], probsfile, 2, cts_each_dim, NULL, 1,
      "static const aom_prob default_delta_lf_probs[DELTA_LF_PROBS]");
#endif
#endif

/* Transform type */
#if CONFIG_EXT_TX
// TODO(yuec): different trees are used depending on selected ext tx set
#else
  // TODO(yuec): intra_ext_tx use different trees depending on the context
  cts_each_dim[0] = EXT_TX_SIZES;
  cts_each_dim[1] = TX_TYPES;
  optimize_entropy_table(&fc.inter_ext_tx[0][0], probsfile, 2, cts_each_dim,
                         av1_ext_tx_tree, 0,
                         "static const aom_prob default_inter_ext_tx_prob"
                         "[EXT_TX_SIZES][TX_TYPES - 1]");
  optimize_cdf_table(&fc.inter_ext_tx[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_inter_ext_tx_prob[EXT_TX_SIZES][CDF_SIZE(TX_"
                     "TYPES)]");
#endif

/* supertx experiment */
#if CONFIG_SUPERTX
  cts_each_dim[0] = PARTITION_SUPERTX_CONTEXTS;
  cts_each_dim[1] = TX_SIZES;
  cts_each_dim[2] = 2;
  optimize_entropy_table(
      &fc.supertx[0][0][0], probsfile, 3, cts_each_dim, NULL, 1,
      "static const aom_prob\n"
      "default_supertx_prob[PARTITION_SUPERTX_CONTEXTS][TX_SIZES]");
  optimize_cdf_table(&fc.supertx[0][0][0], probsfile, 3, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_supertx_cdf[PARTITION_SUPERTX_CONTEXTS][TX_SIZES]"
                     "[CDF_SIZE(2)]");
#endif

/* ext_intra experiment */
#if CONFIG_EXT_INTRA
#if CONFIG_INTRA_INTERP
  cts_each_dim[0] = INTRA_FILTERS + 1;
  cts_each_dim[1] = INTRA_FILTERS;
  optimize_entropy_table(
      &fc.intra_filter[0][0], probsfile, 2, cts_each_dim, av1_intra_filter_tree,
      0,
      "static const aom_prob\n"
      "default_intra_filter_probs[INTRA_FILTERS + 1][INTRA_FILTERS - 1]");
  optimize_cdf_table(&fc.intra_filter[0][0], probsfile, 2, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_intra_filter_cdf[INTRA_FILTERS + "
                     "1][CDF_SIZE(INTRA_FILTERS)]");
#endif
#endif

/* filter_intra experiment */
#if CONFIG_FILTER_INTRA
  cts_each_dim[0] = PLANE_TYPES;
  cts_each_dim[1] = 2;
  optimize_entropy_table(&fc.filter_intra[0][0], probsfile, 2, cts_each_dim,
                         NULL, 1,
                         "static const aom_prob default_filter_intra_probs[2]");
  optimize_cdf_table(
      &fc.filter_intra[0][0], probsfile, 2, cts_each_dim,
      "static const aom_cdf_prob default_filter_intra_cdf[2][CDF_SIZE(2)]");
#endif

#if CONFIG_LV_MAP
  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = NUM_BASE_LEVELS;
  cts_each_dim[3] = COEFF_BASE_CONTEXTS;
  cts_each_dim[4] = 2;
  optimize_entropy_table(&fc.coeff_base[0][0][0][0][0], probsfile, 5,
                         cts_each_dim, NULL, 1,
                         "static const aom_prob "
                         "default_coeff_base[TX_SIZES][PLANE_TYPES][NUM_BASE_"
                         "LEVELS][COEFF_BASE_CONTEXTS]");
  optimize_cdf_table(&fc.coeff_base[0][0][0][0][0], probsfile, 5, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_coeff_base_cdf[TX_SIZES][PLANE_TYPES][NUM_BASE_"
                     "LEVELS][COEFF_BASE_CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = SIG_COEF_CONTEXTS;
  cts_each_dim[3] = 2;
  optimize_entropy_table(
      &fc.nz_map[0][0][0][0], probsfile, 4, cts_each_dim, NULL, 1,
      "static const aom_prob "
      "default_nz_map[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS]");
  optimize_cdf_table(&fc.nz_map[0][0][0][0], probsfile, 4, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_nz_map_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_"
                     "CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = EOB_COEF_CONTEXTS;
  cts_each_dim[3] = 2;
  optimize_entropy_table(
      &fc.eob_flag[0][0][0][0], probsfile, 4, cts_each_dim, NULL, 1,
      "static const aom_prob "
      "default_eob_flag[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS]");
  optimize_cdf_table(&fc.eob_flag[0][0][0][0], probsfile, 4, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_eob_flag_cdf[TX_SIZES][PLANE_TYPES][EOB_COEF_"
                     "CONTEXTS][CDF_SIZE(2)]");

  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = LEVEL_CONTEXTS;
  cts_each_dim[3] = 2;
  optimize_entropy_table(
      &fc.coeff_lps[0][0][0][0], probsfile, 4, cts_each_dim, NULL, 1,
      "static const aom_prob "
      "default_coeff_lps[TX_SIZES][PLANE_TYPES][LEVEL_CONTEXTS]");
  optimize_cdf_table(&fc.coeff_lps[0][0][0][0], probsfile, 4, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_coeff_lps_cdf[TX_SIZES][PLANE_TYPES][LEVEL_"
                     "CONTEXTS][CDF_SIZE(2)]");

#if BR_NODE
  cts_each_dim[0] = TX_SIZES;
  cts_each_dim[1] = PLANE_TYPES;
  cts_each_dim[2] = BASE_RANGE_SETS;
  cts_each_dim[3] = LEVEL_CONTEXTS;
  cts_each_dim[4] = 2;
  optimize_entropy_table(&fc.coeff_br[0][0][0][0][0], probsfile, 5,
                         cts_each_dim, NULL, 1,
                         "static const aom_prob "
                         "default_coeff_br[TX_SIZES][PLANE_TYPES][BASE_RANGE_"
                         "SETS][LEVEL_CONTEXTS]");
  optimize_cdf_table(&fc.coeff_br[0][0][0][0][0], probsfile, 5, cts_each_dim,
                     "static const aom_cdf_prob "
                     "default_coeff_br_cdf[TX_SIZES][PLANE_TYPES][BASE_RANGE_"
                     "SETS][LEVEL_CONTEXTS][CDF_SIZE(2)]");
#endif  // BR_NODE
#endif  // CONFIG_LV_MAP

  fclose(statsfile);
  fclose(logfile);
  fclose(probsfile);

  return 0;
}
