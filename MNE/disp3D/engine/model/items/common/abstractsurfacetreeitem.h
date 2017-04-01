//=============================================================================================================
/**
* @file     abstractsurfacetreeitem.h
* @author   Lorenz Esch <Lorenz.Esch@tu-ilmenau.de>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     March, 2017
*
* @section  LICENSE
*
* Copyright (C) 2017, Lorenz Esch and Matti Hamalainen. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*     * Redistributions of source code must retain the above copyright notice, this list of conditions and the
*       following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
*       the following disclaimer in the documentation and/or other materials provided with the distribution.
*     * Neither the name of MNE-CPP authors nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief     AbstractSurfaceTreeItem class declaration.
*
*/

#ifndef ABSTRACTSURFACETREEITEM_H
#define ABSTRACTSURFACETREEITEM_H

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../../../../disp3D_global.h"
#include "../common/abstracttreeitem.h"
#include "../common/types.h"


//*************************************************************************************************************
//=============================================================================================================
// Qt INCLUDES
//=============================================================================================================

#include <QPointer>


//*************************************************************************************************************
//=============================================================================================================
// Eigen INCLUDES
//=============================================================================================================


//*************************************************************************************************************
//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================

namespace Qt3DCore {
    class QEntity;
}

namespace Qt3DRender {
    class QMaterial;
}


//*************************************************************************************************************
//=============================================================================================================
// DEFINE NAMESPACE DISP3DLIB
//=============================================================================================================

namespace DISP3DLIB
{


//*************************************************************************************************************
//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================

class MetaTreeItem;
class Renderable3DEntity;


//=============================================================================================================
/**
* AbstractSurfaceTreeItem provides a generic brain tree item to hold of brain data (hemi, vertices, tris, etc.) from different sources (FreeSurfer, etc.).
*
* @brief Provides a generic brain tree item.
*/
class DISP3DNEWSHARED_EXPORT AbstractSurfaceTreeItem : public AbstractTreeItem
{
    Q_OBJECT

public:
    typedef QSharedPointer<AbstractSurfaceTreeItem> SPtr;             /**< Shared pointer type for AbstractSurfaceTreeItem class. */
    typedef QSharedPointer<const AbstractSurfaceTreeItem> ConstSPtr;  /**< Const shared pointer type for AbstractSurfaceTreeItem class. */

    //=========================================================================================================
    /**
    * Default constructor.
    *
    * @param[in] iType      The type of the item. See types.h for declaration and definition.
    * @param[in] text       The text of this item. This is also by default the displayed name of the item in a view.
    */
    explicit AbstractSurfaceTreeItem(int iType = Data3DTreeModelItemTypes::AbstractSurfaceItem, const QString& text = "Abstract Surface");

    //=========================================================================================================
    /**
    * Default destructor
    */
    ~AbstractSurfaceTreeItem();

    //=========================================================================================================
    /**
    * AbstractTreeItem functions
    */
    virtual QVariant data(int role = Qt::UserRole + 1) const;
    virtual void setData(const QVariant& value, int role = Qt::UserRole + 1);

    //=========================================================================================================
    /**
    * Call this function whenever you want to change the visibilty of the 3D rendered content.
    *
    * @param[in] state     The visiblity flag.
    */
    virtual void setVisible(bool state);

    //=========================================================================================================
    /**
    * Removes the old material and sets the new material.
    *
    * @param[in] pMaterial     The new material.
    */
    virtual void setMaterial(QPointer<Qt3DRender::QMaterial> pMaterial);

    //=========================================================================================================
    /**
    * Returns the RenderableEntity as a pointer.
    *
    * @return   The RenderableEntity as a pointer.
    */
    virtual QPointer<Renderable3DEntity> getRenderableEntity();

protected:
    //=========================================================================================================
    /**
    * AbstractTreeItem functions
    */
    virtual void initItem();

    //=========================================================================================================
    /**
    * Call this function whenever the alpha value changed.
    *
    * @param[in] fAlpha     The new alpha value.
    */
    virtual void onSurfaceAlphaChanged(float fAlpha);

    //=========================================================================================================
    /**
    * Call this function whenever the inner tesselation value changed.
    *
    * @param[in] fTessInner     The new inner tesselation value.
    */
    virtual void onSurfaceTessInnerChanged(float fTessInner);

    //=========================================================================================================
    /**
    * Call this function whenever the outer tesselation value changed.
    *
    * @param[in] fTessOuter     The new outer tesselation value.
    */
    virtual void onSurfaceTessOuterChanged(float fTessOuter);

    //=========================================================================================================
    /**
    * Call this function whenever the triangle scale value changed.
    *
    * @param[in] fTriangleScale     The triangle scale value.
    */
    virtual void onSurfaceTriangleScaleChanged(float fTriangleScale);

    //=========================================================================================================
    /**
    * Call this function whenever the check box of this item was checked.
    *
    * @param[in] checkState        The current checkstate.
    */
    virtual void onCheckStateChanged(const Qt::CheckState& checkState);

    //=========================================================================================================
    /**
    * Call this function whenever the the translation x of this item changed.
    *
    * @param[in] fTransX        The current x translation.
    */
    virtual void onSurfaceTranslationXChanged(float fTransX);

    //=========================================================================================================
    /**
    * Call this function whenever the the translation y of this item changed.
    *
    * @param[in] fTransY        The current y translation.
    */
    virtual void onSurfaceTranslationYChanged(float fTransY);

    //=========================================================================================================
    /**
    * Call this function whenever the the translation z of this item changed.
    *
    * @param[in] fTransZ        The current z translation.
    */
    virtual void onSurfaceTranslationZChanged(float fTransZ);

    //=========================================================================================================
    /**
    * Call this function whenever the surface color was changed.
    *
    * @param[in] color        The new surface color.
    */
    virtual void onSurfaceColorChanged(const QColor &color);

    //=========================================================================================================
    /**
    * Creates a QByteArray of colors for given color for the input vertices.
    *
    * @param[in] vertices       The vertices information.
    * @param[in] color          The vertex color information.
    *
    * @return The colors per vertex
    */
    virtual MatrixX3f createVertColor(const Eigen::MatrixXf& vertices, const QColor& color = QColor(100,100,100)) const;

    QPointer<Renderable3DEntity>        m_pRenderable3DEntity;                      /**< The surface renderable 3D entity. */
    QPointer<Renderable3DEntity>        m_pRenderable3DEntityNormals;               /**< The normals renderable 3D entity. */

    QPointer<Qt3DRender::QMaterial>     m_pMaterial;                                /**< The material. Ownership belongs to RenderableEntity. */

    bool        m_bUseTesselation;      /**< Whether to use tesselation. */
    bool        m_bRenderNormals;       /**< Whether to render normals. */
};

} //NAMESPACE DISP3DLIB

#endif // ABSTRACTSURFACETREEITEM_H
