/* spoper.c
 * 
 * Copyright (C) 2012 Patrick Alken
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

#include <gsl/gsl_math.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_errno.h>

#include "gsl_spmatrix.h"

static size_t scatter(const gsl_spmatrix *A, const size_t j, size_t *w, double *x,
                      const size_t mark, gsl_spmatrix *C, size_t nz);

int
gsl_spmatrix_scale(gsl_spmatrix *m, const double x)
{
  size_t i;

  for (i = 0; i < m->nz; ++i)
    m->data[i] *= x;

  return GSL_SUCCESS;
} /* gsl_spmatrix_scale() */

int
gsl_spmatrix_minmax(const gsl_spmatrix *m, double *min_out, double *max_out)
{
  double min, max;
  size_t n;

  if (m->nz == 0)
    {
      GSL_ERROR("matrix is empty", GSL_EINVAL);
    }

  min = m->data[0];
  max = m->data[0];

  for (n = 1; n < m->nz; ++n)
    {
      double x = m->data[n];

      if (x < min)
        min = x;

      if (x > max)
        max = x;
    }

  *min_out = min;
  *max_out = max;

  return GSL_SUCCESS;
} /* gsl_spmatrix_minmax() */

/*
gsl_spmatrix_add()
  Add two sparse matrices

Inputs: a - (input) sparse matrix
        b - (input) sparse matrix
        c - (output) a + b

Return: success or error
*/

gsl_spmatrix *
gsl_spmatrix_add(const gsl_spmatrix *a, const gsl_spmatrix *b)
{
  const size_t M = a->size1;
  const size_t N = a->size2;

  if (b->size1 != M || b->size2 != N)
    {
      GSL_ERROR_NULL("matrices must have same dimensions", GSL_EBADLEN);
    }
  else if (a->flags != b->flags)
    {
      GSL_ERROR_NULL("matrices must have same sparse storage format", GSL_EINVAL);
    }
  else if (GSLSP_ISTRIPLET(a))
    {
      GSL_ERROR_NULL("triplet format not yet supported", GSL_EINVAL);
    }
  else
    {
      gsl_spmatrix *c;
      size_t *w = a->work;
      double *x = malloc(M * sizeof(double));
      size_t *Cp, *Ci;
      double *Cd;
      size_t j, p;
      size_t nz = 0; /* number of non-zeros in c */

      c = gsl_spmatrix_alloc_nzmax(M, N, a->nz + b->nz, a->flags);
      if (!c)
        {
          GSL_ERROR_NULL("failed to allocate space for c matrix", GSL_ENOMEM);
        }

      /* initialize w = 0 */
      for (j = 0; j < M; ++j)
        w[j] = 0;

      Ci = c->i;
      Cp = c->p;
      Cd = c->data;

      for (j = 0; j < N; ++j)
        {
          Cp[j] = nz;

          /* x += A(:,j) */
          nz = scatter(a, j, w, x, j + 1, c, nz);

          /* x += B(:,j) */
          nz = scatter(b, j, w, x, j + 1, c, nz);

          for (p = Cp[j]; p < nz; ++p)
            Cd[p] = x[Ci[p]];
        }

      /* finalize last column of c */
      Cp[N] = nz;
      c->nz = nz;

      free(x);

      return c;
    }
} /* gsl_spmatrix_add() */

/*
gsl_spmatrix_d2sp()
  Convert a dense gsl_matrix to sparse (triplet) format

Inputs: S - (output) sparse matrix in triplet format
        A - (input) dense matrix to convert
*/

int
gsl_spmatrix_d2sp(gsl_spmatrix *S, const gsl_matrix *A)
{
  int s = GSL_SUCCESS;
  size_t i, j;

  gsl_spmatrix_set_zero(S);
  S->size1 = A->size1;
  S->size2 = A->size2;

  for (i = 0; i < A->size1; ++i)
    {
      for (j = 0; j < A->size2; ++j)
        {
          double x = gsl_matrix_get(A, i, j);

          if (x != 0.0)
            gsl_spmatrix_set(S, i, j, x);
        }
    }

  return s;
} /* gsl_spmatrix_d2sp() */

/*
gsl_spmatrix_sp2d()
  Convert a sparse matrix to dense format
*/

int
gsl_spmatrix_sp2d(gsl_matrix *A, const gsl_spmatrix *S)
{
  if (A->size1 != S->size1 || A->size2 != S->size2)
    {
      GSL_ERROR("matrix sizes do not match", GSL_EBADLEN);
    }
  else
    {
      gsl_matrix_set_zero(A);

      if (GSLSP_ISTRIPLET(S))
        {
          size_t n;

          for (n = 0; n < S->nz; ++n)
            {
              size_t i = S->i[n];
              size_t j = S->p[n];
              double x = S->data[n];

              gsl_matrix_set(A, i, j, x);
            }
        }
      else
        {
          GSL_ERROR("non-triplet formats not yet supported", GSL_EINVAL);
        }

      return GSL_SUCCESS;
    }
} /* gsl_spmatrix_sp2d() */

/*
scatter()

  Keep a running total x -> x + A(:,j) for adding matrices together in CCS,
which will eventually be stored in C(:,j)

  When a new non-zero element with row index i is found, update C->i with
the row index. C->data is updated only by the calling function after all
matrices have been added via this function.

Inputs: A    - sparse matrix m-by-n
        j    - column index
        w    - keeps track which rows of column j have been added to C;
               initialize to 0 prior to first call
        x    - column vector of length m
        mark -
        C    - output matrix whose jth column will be added to A(:,j)
        nz   - (input/output) number of non-zeros in matrix C

Notes:
1) This function is designed to be called successively when adding multiple
matrices together. Column j of C is stored contiguously as per CCS but not
necessarily in order - ie: the row indices C->i may not be in ascending order.
*/

static size_t
scatter(const gsl_spmatrix *A, const size_t j, size_t *w, double *x,
        const size_t mark, gsl_spmatrix *C, size_t nz)
{
  size_t p;
  size_t *Ai = A->i;
  size_t *Ap = A->p;
  double *Ad = A->data;
  size_t *Ci = C->i;

  for (p = Ap[j]; p < Ap[j + 1]; ++p)
    {
      size_t i = Ai[p];  /* A(i,j) is nonzero */

      if (w[i] < mark)   /* check if this row has been stored in column j yet */
        {
          w[i] = mark;   /* i is new entry in column j */
          Ci[nz++] = i;  /* add i to pattern of C(:,j) */
          x[i] = Ad[p];  /* x(i) = A(i,j) */
        }
      else               /* this (i,j) already exists in C from a previous call */
        {
          x[i] += Ad[p]; /* add A(i,j) to C(i,j) */
        }
    }

  return (nz) ;
} /* scatter() */
