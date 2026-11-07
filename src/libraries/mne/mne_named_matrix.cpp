//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mne_named_matrix.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of @ref MNELIB::MNENamedMatrix.
 *
 * Implements FIFF read/write of the @c FIFF_MNE_ROW_NAMES,
 * @c FIFF_MNE_COL_NAMES and @c FIFF_MNE_NAMED_MATRIX tags, plus the
 * channel-name based row/column lookup used by @ref MNEProjOp and
 * @ref MNECTFCompDataSet.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_named_matrix.h"
#include <fiff/fiff_constants.h>
#include <fiff/fiff_stream.h>
#include <fiff/fiff_tag.h>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace Eigen;
using namespace MNELIB;
using namespace FIFFLIB;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

MNENamedMatrix::MNENamedMatrix()
: nrow(0)
, ncol(0)
{
}

//=============================================================================================================

MNENamedMatrix::MNENamedMatrix(const MNENamedMatrix& p_MneNamedMatrix)
: nrow(p_MneNamedMatrix.nrow)
, ncol(p_MneNamedMatrix.ncol)
, rowlist(p_MneNamedMatrix.rowlist)
, collist(p_MneNamedMatrix.collist)
, data(p_MneNamedMatrix.data)
{
}

//=============================================================================================================

MNENamedMatrix::~MNENamedMatrix()
{
}

//=============================================================================================================

std::unique_ptr<MNENamedMatrix> MNENamedMatrix::build(int nrow,
                                                      int ncol,
                                                      const StringList& rowlist,
                                                      const StringList& collist,
                                                      const Eigen::MatrixXf& data)
{
    auto mat = std::make_unique<MNENamedMatrix>();
    mat->nrow = nrow;
    mat->ncol = ncol;
    mat->rowlist = rowlist;
    mat->collist = collist;
    mat->data = data;
    return mat;
}

//=============================================================================================================

std::unique_ptr<MNENamedMatrix> MNENamedMatrix::pick(const StringList& pickrowlist,
                                                     int picknrow,
                                                     const StringList& pickcollist,
                                                     int pickncol) const
{
    /*
     * Validate: picking by name requires names in the original matrix.
     */
    if (!pickrowlist.empty() && this->rowlist.empty()) {
        qCritical("MNENamedMatrix::pick - Cannot pick rows: no row names in original matrix.");
        return nullptr;
    }
    if (!pickcollist.empty() && this->collist.empty()) {
        qCritical("MNENamedMatrix::pick - Cannot pick columns: no column names in original matrix.");
        return nullptr;
    }

    /*
     * When no pick-list is given, keep all rows / columns.
     */
    if (pickrowlist.empty())
        picknrow = this->nrow;
    if (pickcollist.empty())
        pickncol = this->ncol;

    /*
     * Build row index mapping: for each picked row find its index in the original.
     */
    Eigen::VectorXi pick_row = Eigen::VectorXi::Zero(picknrow);
    StringList my_pickrowlist;

    if (!pickrowlist.empty()) {
        for (int j = 0; j < picknrow; ++j) {
            const std::string& name = pickrowlist[j];
            pick_row[j] = -1;
            for (int k = 0; k < this->nrow; ++k) {
                if ((name == this->rowlist[k])) {
                    pick_row[j] = k;
                    break;
                }
            }
            if (pick_row[j] == -1) {
                qCritical("MNENamedMatrix::pick - Row '%s' not found in original matrix.",
                          name.c_str());
                return nullptr;
            }
        }
        my_pickrowlist = pickrowlist;
    } else {
        for (int k = 0; k < picknrow; ++k)
            pick_row[k] = k;
        my_pickrowlist = this->rowlist;
    }

    /*
     * Build column index mapping analogously.
     */
    Eigen::VectorXi pick_col = Eigen::VectorXi::Zero(pickncol);
    StringList my_pickcollist;

    if (!pickcollist.empty()) {
        for (int j = 0; j < pickncol; ++j) {
            const std::string& name = pickcollist[j];
            pick_col[j] = -1;
            for (int k = 0; k < this->ncol; ++k) {
                if ((name == this->collist[k])) {
                    pick_col[j] = k;
                    break;
                }
            }
            if (pick_col[j] == -1) {
                qCritical("MNENamedMatrix::pick - Column '%s' not found in original matrix.",
                          name.c_str());
                return nullptr;
            }
        }
        my_pickcollist = pickcollist;
    } else {
        for (int k = 0; k < pickncol; ++k)
            pick_col[k] = k;
        my_pickcollist = this->collist;
    }

    /*
     * Assemble the picked data matrix.
     */
    Eigen::MatrixXf pickdata(picknrow, pickncol);
    for (int j = 0; j < picknrow; ++j) {
        const int row = pick_row[j];
        for (int k = 0; k < pickncol; ++k)
            pickdata(j, k) = this->data(row, pick_col[k]);
    }

    return build(picknrow, pickncol, my_pickrowlist, my_pickcollist, pickdata);
}

//=============================================================================================================

std::unique_ptr<MNENamedMatrix> MNENamedMatrix::read(FiffStream& stream,
                                                     const FiffDirNode::SPtr& node,
                                                     int kind)
{
    StringList colnames;
    StringList rownames;
    int ncol = 0;
    int nrow = 0;
    qint32 ndim;
    QVector<qint32> dims;
    MatrixXf data;
    std::optional<FiffTag> t_pTag;
    bool dataFound = false;

    FiffDirNode::SPtr tmp_node = node;

    /*
     * If the node itself is a FIFFB_MNE_NAMED_MATRIX block, read from it.
     * Otherwise look in its first-generation children for such a block.
     */
    if (tmp_node->type == FIFFB_MNE_NAMED_MATRIX) {
        if (t_pTag = tmp_node->find_tag(stream, kind); !t_pTag)
            return nullptr;

        t_pTag->getMatrixDimensions(ndim, dims);
        if (ndim != 2) {
            qCritical("MNENamedMatrix::read - Only two-dimensional matrices are supported.");
            return nullptr;
        }

        data = t_pTag->toFloatMatrix().transpose();
        dataFound = true;
    } else {
        for (int k = 0; k < tmp_node->nchild(); ++k) {
            if (tmp_node->children[k]->type == FIFFB_MNE_NAMED_MATRIX) {
                if (t_pTag = tmp_node->children[k]->find_tag(stream, kind); t_pTag) {
                    t_pTag->getMatrixDimensions(ndim, dims);
                    if (ndim != 2) {
                        qCritical("MNENamedMatrix::read - Only two-dimensional matrices are supported.");
                        return nullptr;
                    }

                    data = t_pTag->toFloatMatrix().transpose();
                    dataFound = true;
                    tmp_node = tmp_node->children[k];
                    break;
                }
            }
        }
        if (!dataFound)
            return nullptr;
    }

    /*
     * Read optional FIFF_MNE_NROW / FIFF_MNE_NCOL dimension tags and
     * cross-check them against the matrix data.
     */
    if (t_pTag = tmp_node->find_tag(stream, FIFF_MNE_NROW); !t_pTag) {
        nrow = static_cast<int>(data.rows());
    } else {
        nrow = *t_pTag->toInt();
        if (nrow != data.rows()) {
            qCritical("MNENamedMatrix::read - FIFF_MNE_NROW tag (%d) conflicts with matrix data (%d).",
                      nrow, static_cast<int>(data.rows()));
            return nullptr;
        }
    }

    if (t_pTag = tmp_node->find_tag(stream, FIFF_MNE_NCOL); !t_pTag) {
        ncol = static_cast<int>(data.cols());
    } else {
        ncol = *t_pTag->toInt();
        if (ncol != data.cols()) {
            qCritical("MNENamedMatrix::read - FIFF_MNE_NCOL tag (%d) conflicts with matrix data (%d).",
                      ncol, static_cast<int>(data.cols()));
            return nullptr;
        }
    }

    /*
     * Read optional row and column name lists.
     */
    if (t_pTag = tmp_node->find_tag(stream, FIFF_MNE_ROW_NAMES); t_pTag) {
        const std::string s = t_pTag->toString();
        rownames = FiffStream::split_name_list(s);
        if (static_cast<int>(rownames.size()) != nrow) {
            qCritical("MNENamedMatrix::read - Row name count (%d) does not match nrow (%d).",
                      static_cast<int>(rownames.size()), nrow);
            return nullptr;
        }
    }

    if (t_pTag = tmp_node->find_tag(stream, FIFF_MNE_COL_NAMES); t_pTag) {
        const std::string s = t_pTag->toString();
        colnames = FiffStream::split_name_list(s);
        if (static_cast<int>(colnames.size()) != ncol) {
            qCritical("MNENamedMatrix::read - Column name count (%d) does not match ncol (%d).",
                      static_cast<int>(colnames.size()), ncol);
            return nullptr;
        }
    }

    return build(nrow, ncol, rownames, colnames, data);
}
