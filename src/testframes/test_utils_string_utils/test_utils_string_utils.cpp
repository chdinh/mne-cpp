//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     test_utils_string_utils.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Tests the string and path helpers against the QString and QFileInfo calls they replace.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/string_utils.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <clocale>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace UTILSLIB;

//=============================================================================================================
/**
 * DECLARE CLASS TestUtilsStringUtils
 *
 * @brief Tests the helpers in utils/string_utils.h.
 */
class TestUtilsStringUtils : public QObject
{
    Q_OBJECT

private slots:
    void trimmedMatchesQString_data();
    void trimmedMatchesQString();
    void splitMatchesQString_data();
    void splitMatchesQString();
    void caseHelpersMatchQString();
    void toDoubleMatchesQString_data();
    void toDoubleMatchesQString();
    void toIntMatchesQString_data();
    void toIntMatchesQString();
    void toDoubleIgnoresTheLocale();
    void utf8PathsRoundTrip();
};

//=============================================================================================================

void TestUtilsStringUtils::trimmedMatchesQString_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("blank") << QStringLiteral(" \t\r\n\v\f ");
    QTest::newRow("both sides") << QStringLiteral("\t MEG 0113 \r\n");
    QTest::newRow("inner kept") << QStringLiteral("a  b");
    QTest::newRow("none") << QStringLiteral("EEG001");
}

//=============================================================================================================

void TestUtilsStringUtils::trimmedMatchesQString()
{
    QFETCH(QString, text);
    QCOMPARE(QString::fromStdString(std::string(trimmed(text.toStdString()))), text.trimmed());
}

//=============================================================================================================

void TestUtilsStringUtils::splitMatchesQString_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("skipEmpty");
    for (const bool skip : {false, true}) {
        const char* suffix = skip ? " skip empty" : "";
        QTest::addRow("empty%s", suffix) << QString() << skip;
        QTest::addRow("one field%s", suffix) << QStringLiteral("Fp1") << skip;
        QTest::addRow("fields%s", suffix) << QStringLiteral("Fp1,Fp2,Cz") << skip;
        QTest::addRow("empty fields%s", suffix) << QStringLiteral(",Fp1,,Cz,") << skip;
        QTest::addRow("only separators%s", suffix) << QStringLiteral(",,") << skip;
    }
}

//=============================================================================================================

void TestUtilsStringUtils::splitMatchesQString()
{
    QFETCH(QString, text);
    QFETCH(bool, skipEmpty);
    const QStringList expected = text.split(QLatin1Char(','), skipEmpty ? Qt::SkipEmptyParts : Qt::KeepEmptyParts);
    const std::vector<std::string> actual = split(text.toStdString(), ',', skipEmpty);
    QCOMPARE(static_cast<qsizetype>(actual.size()), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        QCOMPARE(QString::fromStdString(actual[i]), expected[static_cast<qsizetype>(i)]);
    }
}

//=============================================================================================================

void TestUtilsStringUtils::caseHelpersMatchQString()
{
    const QString mixed = QStringLiteral("Brain-Vision_V1.0 eeg");
    QCOMPARE(QString::fromStdString(toLower(mixed.toStdString())), mixed.toLower());
    QVERIFY(equalsIgnoreCase("NIFTI", "nifti"));
    QVERIFY(equalsIgnoreCase("", ""));
    QVERIFY(!equalsIgnoreCase("nii", "nii.gz"));
    QVERIFY(!equalsIgnoreCase("abc", "abd"));
    QCOMPARE(equalsIgnoreCase("Fp1", "fP1"), QString::compare(QStringLiteral("Fp1"), QStringLiteral("fP1"), Qt::CaseInsensitive) == 0);
    // Bytes outside ASCII are compared as they are, so UTF-8 text never changes length.
    QCOMPARE(toLower("\xc3\x84"), std::string("\xc3\x84"));
}

//=============================================================================================================

void TestUtilsStringUtils::toDoubleMatchesQString_data()
{
    QTest::addColumn<QString>("text");
    for (const char* text : {"0", "-1.5", "1e-3", "  2.25 ", "+7", ".5", "1.", "3.4e38", "1e400", "nan", "inf", "-inf",
                             "", " ", "abc", "1.5x", "1,5", "--1", "0x10", "1e-320", "4.9e-324", "1e-400", "1.7976931348623157e308",
                             "2.2250738585072014e-308", "-0", "1e", "e5", "+-1"}) {
        QTest::newRow(text[0] ? text : "(empty)") << QString::fromLatin1(text);
    }
}

//=============================================================================================================

void TestUtilsStringUtils::toDoubleMatchesQString()
{
    QFETCH(QString, text);
    bool ok = false;
    const double expected = text.toDouble(&ok);
    const std::optional<double> actual = toDouble(text.toStdString());
    QCOMPARE(actual.has_value(), ok);
    if (ok) {
        if (std::isnan(expected)) {
            QVERIFY(std::isnan(*actual));
        } else {
            QCOMPARE(*actual, expected);
        }
    }
}

//=============================================================================================================

void TestUtilsStringUtils::toIntMatchesQString_data()
{
    QTest::addColumn<QString>("text");
    for (const char* text : {"0", "-42", " 17 ", "+5", "2147483647", "-2147483648", "2147483648", "", "1.0", "12a", "0x1F"}) {
        QTest::newRow(text[0] ? text : "(empty)") << QString::fromLatin1(text);
    }
}

//=============================================================================================================

void TestUtilsStringUtils::toIntMatchesQString()
{
    QFETCH(QString, text);
    bool ok = false;
    const int expected = text.toInt(&ok);
    const std::optional<int> actual = toInt(text.toStdString());
    QCOMPARE(actual.has_value(), ok);
    if (ok) {
        QCOMPARE(*actual, expected);
    }
}

//=============================================================================================================

void TestUtilsStringUtils::toDoubleIgnoresTheLocale()
{
    // QCoreApplication adopts the environment's locale, where the decimal separator may be a comma.
    const std::string previous = std::setlocale(LC_ALL, nullptr);
    bool german = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252"}) {
        german = german || std::setlocale(LC_ALL, name) != nullptr;
    }
    const std::optional<double> point = toDouble("1.5");
    const std::optional<double> comma = toDouble("1,5");
    std::setlocale(LC_ALL, previous.c_str());
    if (!german) {
        QSKIP("No German locale installed");
    }
    QCOMPARE(point, std::optional<double>(1.5));
    QVERIFY(!comma);
}

//=============================================================================================================

void TestUtilsStringUtils::utf8PathsRoundTrip()
{
    // On Windows std::filesystem::path(std::string) assumes the ANSI code page, not UTF-8.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString name = QStringLiteral("sub-\u00e4\u00f6_\u65e5\u672c.fif");
    const std::string utf8 = QDir(dir.path()).filePath(name).toStdString();

    const std::filesystem::path path = pathFromUtf8(utf8);
    QCOMPARE(utf8FromPath(path), utf8);
    {
        std::ofstream out(path, std::ios::binary);
        out << "x";
    }
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(name)));
    QCOMPARE(utf8FromPath(path.filename()), name.toStdString());
    QCOMPARE(utf8FromPath(path.extension()), std::string(".fif"));
}

//=============================================================================================================
// MAIN
//=============================================================================================================

QTEST_GUILESS_MAIN(TestUtilsStringUtils)
#include "test_utils_string_utils.moc"
