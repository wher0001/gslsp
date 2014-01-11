/* test.c
 * 
 * Copyright (C) 2012-2014 Patrick Alken
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_test.h>
#include <gsl/gsl_blas.h>

#include "gsl_spmatrix.h"

/*
create_random_sparse()
  Create a random sparse matrix with approximately
M*N*density non-zero entries

Inputs: M       - number of rows
        N       - number of columns
        density - sparse density \in [0,1]
                  0 = no non-zero entries
                  1 = all m*n entries are filled
        r       - random number generator

Return: pointer to sparse matrix in triplet format (must be freed by caller)

Notes:
1) non-zero matrix entries are uniformly distributed in [0,1]
*/

static gsl_spmatrix *
create_random_sparse(const size_t M, const size_t N, const double density,
                     const gsl_rng *r)
{
  gsl_spmatrix *m = gsl_spmatrix_alloc(M, N);
  size_t nnzwanted = (size_t) round(M * N * GSL_MIN(density, 1.0));
  size_t n = 0;

  while (n <= nnzwanted)
    {
      /* generate a random row and column */
      size_t i = gsl_rng_uniform(r) * M;
      size_t j = gsl_rng_uniform(r) * N;
      double x;

      assert(i < M);
      assert(j < N);

      /* check if this position is already filled */
      if (gsl_spmatrix_get(m, i, j) != 0.0)
        continue;

      /* generate random m_{ij} and add it */
      x = gsl_rng_uniform(r);
      gsl_spmatrix_set(m, i, j, x);
      ++n;
    }

  return m;
} /* create_random_sparse() */

static void
create_random_vector(gsl_vector *v, const gsl_rng *r)
{
  size_t i;

  for (i = 0; i < v->size; ++i)
    {
      double x = gsl_rng_uniform(r);
      gsl_vector_set(v, i, x);
    }
} /* create_random_vector() */

int
test_vectors(gsl_vector *observed, gsl_vector *expected, const double tol,
             const char *str)
{
  int s = 0;
  size_t N = observed->size;
  size_t i;

  for (i = 0; i < N; ++i)
    {
      double x_obs = gsl_vector_get(observed, i);
      double x_exp = gsl_vector_get(expected, i);

      gsl_test_rel(x_obs, x_exp, tol, "N=%zu i=%zu %s", N, i, str);
    }

  return s;
} /* test_vectors() */

static void
test_getset(const size_t M, const size_t N)
{
  int status;
  size_t i, j;
  size_t k = 0;
  gsl_spmatrix *m = gsl_spmatrix_alloc(M, N);

  status = 0;
  for (i = 0; i < M; ++i)
    {
      for (j = 0; j < N; ++j)
        {
          double x = (double) ++k;
          double y;

          gsl_spmatrix_set(m, i, j, x);
          y = gsl_spmatrix_get(m, i, j);

          if (x != y)
            status = 1;
        }
    }

  gsl_test(status, "test_getset: M=%zu N=%zu _get != _set", M, N);

  gsl_spmatrix_free(m);
} /* test_getset() */

static void
test_memcpy(const size_t M, const size_t N, const gsl_rng *r)
{
  int status;
  gsl_spmatrix *a = create_random_sparse(M, N, 0.2, r);
  gsl_spmatrix *b, *c, *d;
  
  b = gsl_spmatrix_memcpy(a);

  status = gsl_spmatrix_equal(a, b) != 1;
  gsl_test(status, "test_memcpy: M=%zu N=%zu triplet format", M, N);

  c = gsl_spmatrix_compcol(a);
  d = gsl_spmatrix_memcpy(c);

  status = gsl_spmatrix_equal(c, d) != 1;
  gsl_test(status, "test_memcpy: M=%zu N=%zu compressed column format", M, N);

  gsl_spmatrix_free(a);
  gsl_spmatrix_free(b);
  gsl_spmatrix_free(c);
  gsl_spmatrix_free(d);
} /* test_memcpy() */

void
test_dgemv(const double alpha, const double beta, const gsl_rng *r)
{
  size_t N_max = 50;
  gsl_matrix *A = gsl_matrix_alloc(N_max, N_max);
  gsl_vector *x = gsl_vector_alloc(N_max);
  gsl_vector *y0 = gsl_vector_alloc(N_max);
  gsl_vector *y1 = gsl_vector_alloc(N_max);
  gsl_vector *y2 = gsl_vector_alloc(N_max);
  size_t N, M;

  for (M = 1; M <= N_max; ++M)
    {
      gsl_vector_view y = gsl_vector_subvector(y0, 0, M);
      gsl_vector_view y_gsl = gsl_vector_subvector(y1, 0, M);
      gsl_vector_view y_sp = gsl_vector_subvector(y2, 0, M);

      for (N = 1; N <= N_max; ++N)
        {
          gsl_matrix_view Av = gsl_matrix_submatrix(A, 0, 0, M, N);
          gsl_vector_view xv = gsl_vector_subvector(x, 0, N);
          gsl_spmatrix *mt = create_random_sparse(M, N, 0.2, r);
          gsl_spmatrix *mc;

          /* create random dense vectors */
          create_random_vector(&xv.vector, r);
          create_random_vector(&y.vector, r);

          gsl_vector_memcpy(&y_gsl.vector, &y.vector);
          gsl_vector_memcpy(&y_sp.vector, &y.vector);

          /* copy mt into A */
          gsl_spmatrix_sp2d(&Av.matrix, mt);

          /* compute y = alpha*A*x + beta*y0 with gsl */
          gsl_blas_dgemv(CblasNoTrans, alpha, &Av.matrix, &xv.vector, beta, &y_gsl.vector);

          /* compute y = alpha*A*x + beta*y0 with spblas/triplet */
          gsl_spblas_dgemv(alpha, mt, &xv.vector, beta, &y_sp.vector);
          test_vectors(&y_sp.vector, &y_gsl.vector, 1.0e-12,
                       "test_dgemv: triplet format");

          /* compute y = alpha*A*x + beta*y0 with spblas/compcol */
          mc = gsl_spmatrix_compcol(mt);
          gsl_vector_memcpy(&y_sp.vector, &y.vector);
          gsl_spblas_dgemv(alpha, mc, &xv.vector, beta, &y_sp.vector);
          test_vectors(&y_sp.vector, &y_gsl.vector, 1.0e-12,
                       "test_dgemv: compressed column format");

          gsl_spmatrix_free(mc);
          gsl_spmatrix_free(mt);
        }
    }

  gsl_matrix_free(A);
  gsl_vector_free(x);
  gsl_vector_free(y0);
  gsl_vector_free(y1);
  gsl_vector_free(y2);
} /* test_dgemv() */

int
main()
{
  gsl_rng *r = gsl_rng_alloc(gsl_rng_default);

  test_memcpy(10, 10, r);
  test_memcpy(10, 15, r);
  test_memcpy(53, 213, r);

  test_getset(20, 20);
  test_getset(30, 20);
  test_getset(15, 210);

  test_dgemv(1.0, 0.0, r);
  test_dgemv(2.4, -0.5, r);
  test_dgemv(0.1, 10.0, r);

  gsl_rng_free(r);

  exit (gsl_test_summary());
} /* main() */
