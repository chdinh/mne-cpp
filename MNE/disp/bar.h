//=============================================================================================================
/**
* @file     bar.h
* @author   Ricky Tjen <ricky270@student.sgu.ac.id>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>;
* @version  1.0
* @date     April, 2016
*
* @section  LICENSE
*
* Copyright (C) 2016, Ricky Tjen and Matti Hamalainen. All rights reserved.
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
* @brief    Bar class declaration
*
*/

#ifndef BAR_H
#define BAR_H

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "disp_global.h"


//*************************************************************************************************************
//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QWidget>
#include <QString>
#include <QGridLayout>
#include <QSharedPointer>
#include <QMainWindow>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLegend>
#include <QtCharts/QBarCategoryAxis>


//*************************************************************************************************************
//=============================================================================================================
// Eigen INCLUDES
//=============================================================================================================

#include <Eigen/Core>


//*************************************************************************************************************
//=============================================================================================================
// DEFINE NAMESPACE DISPLIB
//=============================================================================================================

namespace DISPLIB
{


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

QT_CHARTS_USE_NAMESPACE


//*************************************************************************************************************
//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================


//=============================================================================================================
/**
* Histogram display using Qtcharts, similar to matlab bar graph
*
* @brief Bar class for histogram display using QtCharts
*/
class DISPSHARED_EXPORT Bar : public QWidget
{

public:
    typedef QSharedPointer<Bar> SPtr;            /**< Shared pointer type for Bar. */
    typedef QSharedPointer<const Bar> ConstSPtr; /**< Const shared pointer type for Bar. */

    //=========================================================================================================
    /**
    * The constructor.
    */
    Bar(const QString& title = "", QWidget* parent = 0);

    //=========================================================================================================
    /**
    * Sets new data to the bar chart using QtCharts
    *
    * @param[in]  matClassLimitData      vector input filled with class limits
    * @param[in]  matClassFrequencyData  vector input filled with class frequency to the corresponding class
    * @param[in]  iPrecisionValue        user input to determine the amount of digits of coefficient shown in the histogram
    */
    template<typename T>
    void setData(const Eigen::Matrix<T, Eigen::Dynamic, 1>& matClassLimitData, const Eigen::Matrix<int, Eigen::Dynamic, 1>& matClassFrequencyData, int iPrecisionValue);
    template<typename T>
    void setData(const Eigen::Matrix<T, 1, Eigen::Dynamic>& matClassLimitData, const Eigen::Matrix<int, 1, Eigen::Dynamic>& matClassFrequencyData, int iPrecisionValue);

private:
    QChart*             m_pChart;
    QBarCategoryAxis*   m_pAxis;

    //=========================================================================================================
    /**
    * Updates the bar plot with the new data
    *
    * @param[in]  matClassLimitData      vector input filled with class limits
    * @param[in]  matClassFrequencyData  vector input filled with class frequency to the corresponding class
    * @param[in]  iPrecisionValue        user input to determine the amount of digits of coefficient shown in the histogram
    */
    template<typename T>
    void updatePlot(const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& matClassLimitData, const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic>& matClassFrequencyData, int iPrecisionValue);

    //=========================================================================================================
    /**
    * splitCoefficientAndExponent takes in QVector value of coefficient and exponent (example: 1.2e-10) and finds the coefficient (1.2) and the appropriate exponent (-12), normalize the exponents to either the lowest or highest exponent in the list then places the values in two separate QVectors
    *
    * @param[in]  matClassLimitData      vector input filled with values of class limits (in coefficient and exponent form)
    * @param[in]  iClassCount            user input to determine the amount of classes in the histogram
    * @param[out] vecCoefficientResults  vector filled with values of coefficient only
    * @param[out] vecExponentResults     vector filled with values of exponent only
    */

//      void splitCoefficientAndExponent (const Eigen::Matrix<T, Eigen::Dynamic, 1>& matClassLimitData, int iClassAmount, Eigen::VectorXd& vecCoefficientResults, Eigen::VectorXi& vecExponentValues);
//      template<typename T>
//      void splitCoefficientAndExponent (const Eigen::Matrix<T, 1, Eigen::Dynamic>& matClassLimitData, int iClassAmount, Eigen::VectorXd& vecCoefficientResults, Eigen::VectorXi& vecExponentValues);
//      template<typename T>
//      void splitCoefficientAndExponent (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& matClassLimitData, int iClassAmount, Eigen::VectorXd& vecCoefficientResults, Eigen::VectorXi& vecExponentValues);
    template<typename T>
    void splitCoefficientAndExponent (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& matClassLimitData, int iClassAmount, Eigen::VectorXd& vecCoefficientResults, Eigen::VectorXi& vecExponentValues);

};


//*************************************************************************************************************
//=============================================================================================================
// INLINE DEFINITIONS
//=============================================================================================================

template<typename T>
void Bar::setData(const Eigen::Matrix<T, Eigen::Dynamic, 1>& matClassLimitData, const Eigen::Matrix<int, Eigen::Dynamic, 1>& matClassFrequencyData, int iPrecisionValue)
{
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> matrixName(matClassLimitData.rows(),1);
    matrixName.col(0)= matClassLimitData;

    this->updatePlot(matrixName, matClassFrequencyData, iPrecisionValue);
}


//*************************************************************************************************************

template<typename T>
void Bar::setData(const Eigen::Matrix<T, 1, Eigen::Dynamic>& matClassLimitData, const Eigen::Matrix<int, 1, Eigen::Dynamic>& matClassFrequencyData, int iPrecisionValue)
{
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> matrixName(1, matClassLimitData.cols());
    matrixName.row(0)= matClassLimitData;

    this->updatePlot(matrixName, matClassFrequencyData, iPrecisionValue);
}


//*************************************************************************************************************

template<typename T>
void Bar::updatePlot(const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& matClassLimitData, const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic>& matClassFrequencyData, int iPrecisionValue)
{
    Eigen::VectorXd resultDisplayValues;
    Eigen::VectorXi resultExponentValues;
    int iClassAmount = matClassFrequencyData.rows();
    this->splitCoefficientAndExponent (matClassLimitData, iClassAmount, resultDisplayValues, resultExponentValues);

    //Setup legends
    QString histogramExponent;
    histogramExponent = "X-axis scale: 10e" + QString::number(resultExponentValues(0));
    QBarSet* set = new QBarSet(histogramExponent);
    QStringList categories;
    QString currentLimits;
    int classFreq;

    for (int kr=0; kr < iClassAmount; kr++)
    {
        classFreq = matClassFrequencyData(kr);
        currentLimits = ((QString::number(resultDisplayValues(kr), 'g' ,iPrecisionValue) + " to " + (QString::number(resultDisplayValues(kr+1), 'g', iPrecisionValue))));
        categories << currentLimits;
        *set << classFreq;
    }

    //Create new series, then clear the plot and update with new data
    QBarSeries *series = new QBarSeries();
    series->append(set);

    m_pChart->removeAllSeries();
    m_pChart->addSeries(series);

    m_pAxis->clear();
    m_pAxis->append(categories);
    m_pChart->createDefaultAxes();
    m_pChart->setAxisX(m_pAxis, series);
}


//*************************************************************************************************************

template <typename T>
void Bar::splitCoefficientAndExponent (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& matClassLimitData, int iClassAmount, Eigen::VectorXd& vecCoefficientResults, Eigen::VectorXi& vecExponentValues)
{
    vecCoefficientResults.resize(iClassAmount + 1);
    vecExponentValues.resize(iClassAmount + 1);
    double originalValue(0.0),
           limitDisplayValue(0.0),
           doubleExponentValue(0.0);
    int    limitExponentValue(0);
    for (int ir=0; ir <= iClassAmount; ir++)
    {
        originalValue = matClassLimitData(ir);
        if (originalValue == 0.0)                          //mechanism to guard against evaluation of log(0.0) which is infinity
        {
            doubleExponentValue = 0.0;
        }
        else
        {
            doubleExponentValue = log10(abs(originalValue));                    //return the exponent value in double
        }
        limitExponentValue = round(doubleExponentValue);                        //round the exponent value to the nearest signed integer
        limitDisplayValue = originalValue * (pow(10,-(limitExponentValue)));    //display value is derived from multiplying class limit with inverse 10 to the power of negative exponent
        vecCoefficientResults(ir) = limitDisplayValue;                         //append the display value to the return vector
        vecExponentValues(ir) = limitExponentValue;                            //append the exponent value to the return vector
    }

    int lowestExponentValue{0},
        highestExponentValue{0};
    for (int ir=0; ir <= iClassAmount; ir++)
    {
        if (vecExponentValues(ir) < lowestExponentValue)
        {
            lowestExponentValue = vecExponentValues(ir);        //find lowest exponent value to normalize display values for negative exponent
        }
        if (vecExponentValues(ir) > highestExponentValue)       //find highest exponent value to normalize display values for positive exponent
        {
            highestExponentValue = vecExponentValues(ir);
        }
    }

    if (highestExponentValue == 0)
    {
        for (int ir=0; ir <= iClassAmount; ir++)
        {
            while (vecExponentValues(ir) > lowestExponentValue)     //normalize the values by multiplying the display value by 10 and reducing the exponentValue by 1 until exponentValue reach the lowestExponentValue
            {
                vecCoefficientResults(ir) = vecCoefficientResults(ir) * 10;
                vecExponentValues(ir)--;
            }
        }
    }
    if (lowestExponentValue == 0)
    {
        for (int ir=0; ir <= iClassAmount; ir++)
        {
            while (vecExponentValues(ir) < highestExponentValue)
            {
                vecCoefficientResults(ir) = vecCoefficientResults(ir) / 10;
                vecExponentValues(ir)++;
            }
        }
    }
}
}


//=========================================================================================================
// NAMESPACE

#endif // BAR_H
