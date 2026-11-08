//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     inv_dipole_fit_settings.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of @ref INVLIB::InvDipoleFitSettings (default values, argv parsing, integrity check, usage banner).
 *
 * Implements the default-value initialiser, the
 * @c mne_dipole_fit-compatible argv parser, the per-option validator
 * that enforces mandatory combinations (e.g. noise covariance vs.
 * ad-hoc per-channel-type standard deviations) and the human-readable
 * usage banner used by the @c mne_dipole_fit driver binary.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "inv_dipole_fit_settings.h"
#include <utils/logger/log.h>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace Eigen;
using namespace INVLIB;

//=============================================================================================================
// DEFINE STATIC HELPERS
//=============================================================================================================

namespace
{

//=============================================================================================================
/**
 * Parses a floating point command line argument.
 *
 * @param[in]  arg     The argument text.
 * @param[out] value   Receives the parsed value; untouched on failure.
 *
 * @return True if the whole argument was a number.
 */
bool parseFloat(const char* arg, float& value)
{
    bool bOk = false;
    const float parsed = QString::fromUtf8(arg).toFloat(&bOk);
    if (bOk) {
        value = parsed;
    }

    return bOk;
}

//=============================================================================================================
/**
 * Parses an integer command line argument.
 *
 * @param[in]  arg     The argument text.
 * @param[out] value   Receives the parsed value; untouched on failure.
 *
 * @return True if the whole argument was a number.
 */
bool parseInt(const char* arg, int& value)
{
    bool bOk = false;
    const int parsed = QString::fromUtf8(arg).toInt(&bOk);
    if (bOk) {
        value = parsed;
    }

    return bOk;
}

//=============================================================================================================
/**
 * Parses a colon separated triplet such as "1.0:2.0:3.0".
 *
 * @param[in]  arg      The argument text.
 * @param[out] values   Receives the three parsed values; untouched on failure.
 *
 * @return True if exactly three numbers were given.
 */
bool parseTriplet(const char* arg, Eigen::Vector3f& values)
{
    const QStringList parts = QString::fromUtf8(arg).split(':');
    if (parts.size() != 3) {
        return false;
    }

    Eigen::Vector3f parsed;
    for (int i = 0; i < 3; ++i) {
        bool bOk = false;
        parsed[i] = parts.at(i).toFloat(&bOk);
        if (!bOk) {
            return false;
        }
    }

    values = parsed;

    return true;
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

InvDipoleFitSettings::InvDipoleFitSettings()
{
    initMembers();
}

//=============================================================================================================

InvDipoleFitSettings::InvDipoleFitSettings(int* argc, char** argv)
{
    initMembers();

    if (!check_args(argc, argv))
        return;

    checkIntegrity();
}

//=============================================================================================================

InvDipoleFitSettings::~InvDipoleFitSettings()
{
}

//=============================================================================================================

void InvDipoleFitSettings::initMembers()
{
    // Init origin
    r0 << 0.0f, 0.0f, 0.04f;

    filter.filter_on = true;
    filter.size = 4096;
    filter.taper_size = 2048;
    filter.highpass = 0.0;
    filter.highpass_width = 0.0;
    filter.lowpass = 40.0;
    filter.lowpass_width = 5.0;
    filter.eog_highpass = 0.0;
    filter.eog_highpass_width = 0.0;
    filter.eog_lowpass = 40.0;
    filter.eog_lowpass_width = 5.0;

    accurate = false; /**< Use accurate coil definitions?. */

    guess_rad = 0.080f;
    guess_mindist = 0.010f;
    guess_exclude = 0.020f;
    guess_grid = 0.010f;

    grad_std = 5e-13f;
    mag_std = 20e-15f;
    eeg_std = 0.2e-6f;
    diagnoise = false;

    is_raw = false;
    include_meg = false;
    include_eeg = false;
    tmin = -2 * BIG_TIME;
    tmax = 2 * BIG_TIME;
    tstep = -1.0;
    integ = 0.0;
    bmin = BIG_TIME;
    bmax = BIG_TIME;
    do_baseline = false;
    setno = 1;
    verbose = false;
    omit_data_proj = false;


    eeg_sphere_rad = 0.09f;
    scale_eeg_pos = false;
    mag_reg = 0.1f;
    fit_mag_dipoles = false;

    grad_reg = 0.1f;
    eeg_reg = 0.1f;

    gui = false;
}

//=============================================================================================================

bool InvDipoleFitSettings::checkIntegrity()
{
    do_baseline = (bmin < BIG_TIME && bmax < BIG_TIME);

    if (measname.isEmpty()) {
        UTILSLIB::Log::critical("Data file name missing. Please specify one using the --meas option.");
        return false;
    }
    if (guessname.isEmpty()) {
        if (bemname.isEmpty() && !guess_surfname.isEmpty() && mriname.isEmpty()) {
            UTILSLIB::Log::critical("Please specify the MRI/head coordinate transformation with the --mri option");
            return false;
        }
    }
    if (!include_meg && !include_eeg) {
        UTILSLIB::Log::critical("Specify one or both of the --eeg and --meg options");
        return false;
    }
    // Calling this again (e.g. once per fit) must not add the data file's projections twice
    if (!omit_data_proj && !std::ranges::contains(projnames, measname))
        projnames.insert(projnames.begin(), measname);

    if (!bemname.isEmpty())
        UTILSLIB::Log::info("BEM              : {}", bemname.toStdString());
    else {
        UTILSLIB::Log::info("Sphere model     : origin at ({: 7.2f} {: 7.2f} {: 7.2f}) mm", 1000 * r0[0], 1000 * r0[1], 1000 * r0[2]);
    }
    UTILSLIB::Log::info("Using {} MEG coil definitions.", accurate ? "accurate" : "standard");
    if (!mriname.isEmpty())
        UTILSLIB::Log::info("MRI transform    : {}", mriname.toStdString());
    if (!guessname.isEmpty())
        UTILSLIB::Log::info("Guesses          : {}", guessname.toStdString());
    else {
        if (!guess_surfname.isEmpty())
            UTILSLIB::Log::info("Guess space bounded by {}", guess_surfname.toStdString());
        else
            UTILSLIB::Log::info("Spherical guess space, rad = {:.1f} mm", 1000 * guess_rad);
        UTILSLIB::Log::info("Guess grid       : {:6.1f} mm", 1000 * guess_grid);
        if (guess_mindist > 0.0)
            UTILSLIB::Log::info("Guess mindist    : {:6.1f} mm", 1000 * guess_mindist);
        if (guess_exclude > 0)
            UTILSLIB::Log::info("Guess exclude    : {:6.1f} mm", 1000 * guess_exclude);
    }
    UTILSLIB::Log::info("Data             : {}", measname.toStdString());
    if (projnames.size() > 0) {
        UTILSLIB::Log::info("SSP sources      :");
        for (const QString& projname : projnames)
            UTILSLIB::Log::info("\t{}", projname.toStdString());
    }
    if (!badname.isEmpty())
        UTILSLIB::Log::info("Bad channels     : {}", badname.toStdString());
    if (do_baseline)
        UTILSLIB::Log::info("Baseline         : {:10.2f} ... {:10.2f} ms", 1000 * bmin, 1000 * bmax);
    if (!noisename.isEmpty()) {
        UTILSLIB::Log::info("Noise covariance : {}", noisename.toStdString());
        if (include_meg) {
            if (mag_reg > 0.0)
                UTILSLIB::Log::info("\tNoise-covariance regularization (mag)     : {:<5.2f}", mag_reg);
            if (grad_reg > 0.0)
                UTILSLIB::Log::info("\tNoise-covariance regularization (grad)    : {:<5.2f}", grad_reg);
        }
        if (include_eeg && eeg_reg > 0.0)
            UTILSLIB::Log::info("\tNoise-covariance regularization (EEG)     : {:<5.2f}", eeg_reg);
    }
    if (fit_mag_dipoles)
        UTILSLIB::Log::info("Fit data with magnetic dipoles");
    if (!dipname.isEmpty())
        UTILSLIB::Log::info("dip output      : {}", dipname.toStdString());
    if (!bdipname.isEmpty())
        UTILSLIB::Log::info("bdip output     : {}", bdipname.toStdString());
    return true;
}

//=============================================================================================================

void InvDipoleFitSettings::usage(const char* name)
{
    UTILSLIB::Log::info("usage: {} [options]", name);
    UTILSLIB::Log::info("This is a program for sequential single dipole fitting.");
    UTILSLIB::Log::info("\nInput data:\n");
    UTILSLIB::Log::info("\t--meas name       specify an evoked-response data file");
    UTILSLIB::Log::info("\t--set   no        evoked data set number to use (default: 1)");
    UTILSLIB::Log::info("\t--bad name        take bad channel list from here");

    UTILSLIB::Log::info("\nModality selection:\n");
    UTILSLIB::Log::info("\t--meg             employ MEG data in fitting");
    UTILSLIB::Log::info("\t--eeg             employ EEG data in fitting");

    UTILSLIB::Log::info("\nTime scale selection:\n");
    UTILSLIB::Log::info("\t--tmin  time/ms   specify the starting analysis time");
    UTILSLIB::Log::info("\t--tmax  time/ms   specify the ending analysis time");
    UTILSLIB::Log::info("\t--tstep time/ms   specify the time step between frames (default 1/(sampling frequency))");
    UTILSLIB::Log::info("\t--integ time/ms   specify the time integration for each frame (default 0)");

    UTILSLIB::Log::info("\nPreprocessing:\n");
    UTILSLIB::Log::info("\t--bmin  time/ms   specify the baseline starting time (evoked data only)");
    UTILSLIB::Log::info("\t--bmax  time/ms   specify the baseline ending time (evoked data only)");
    UTILSLIB::Log::info("\t--proj name       Load the linear projection from here");
    UTILSLIB::Log::info("\t                  Multiple projections can be specified.");
    UTILSLIB::Log::info("\t                  The data file will be automatically included, unless --noproj is present.");
    UTILSLIB::Log::info("\t--noproj          Do not load the projection from the data file, just those given with the --proj option.");
    UTILSLIB::Log::info("\n\tFiltering (raw data only):\n");
    UTILSLIB::Log::info("\t--filtersize size desired filter length (default = {})", filter.size);
    UTILSLIB::Log::info("\t--highpass val/Hz highpass corner (default = {:6.1f} Hz)", filter.highpass);
    UTILSLIB::Log::info("\t--lowpass  val/Hz lowpass  corner (default = {:6.1f} Hz)", filter.lowpass);
    UTILSLIB::Log::info("\t--lowpassw val/Hz lowpass transition width (default = {:6.1f} Hz)", filter.lowpass_width);
    UTILSLIB::Log::info("\t--filteroff       do not filter the data");

    UTILSLIB::Log::info("\nNoise specification:\n");
    UTILSLIB::Log::info("\t--noise name      take the noise-covariance matrix from here");
    UTILSLIB::Log::info("\t--gradnoise val   specify a gradiometer noise value in fT/cm");
    UTILSLIB::Log::info("\t--magnoise val    specify a magnetometer noise value in fT");
    UTILSLIB::Log::info("\t--eegnoise val    specify an EEG value in uV");
    UTILSLIB::Log::info("\t                  NOTE: The above will be used only if --noise is missing");
    UTILSLIB::Log::info("\t--diagnoise       omit off-diagonal terms from the noise-covariance matrix");
    UTILSLIB::Log::info("\t--reg amount      Apply regularization to the noise-covariance matrix (same fraction for all channels).");
    UTILSLIB::Log::info("\t--gradreg amount  Apply regularization to the MEG noise-covariance matrix (planar gradiometers, default = {:6.2f}).", grad_reg);
    UTILSLIB::Log::info("\t--magreg amount   Apply regularization to the MEG noise-covariance matrix (axial gradiometers and magnetometers, default = {:6.2f}).", mag_reg);
    UTILSLIB::Log::info("\t--eegreg amount   Apply regularization to the EEG noise-covariance matrix (default = {:6.2f}).", eeg_reg);

    UTILSLIB::Log::info("\nForward model:\n");
    UTILSLIB::Log::info("\t--mri name        take head/MRI coordinate transform from here (Neuromag MRI description file)");
    UTILSLIB::Log::info("\t--bem  name       BEM model name");
    UTILSLIB::Log::info("\t--origin x:y:z/mm use a sphere model with this origin (head coordinates/mm)");
    UTILSLIB::Log::info("\t--eegscalp        scale the electrode locations to the surface of the scalp when using a sphere model");
    UTILSLIB::Log::info("\t--eegmodels name  read EEG sphere model specifications from here.");
    UTILSLIB::Log::info("\t--eegmodel  name  name of the EEG sphere model to use (default : Default)");
    UTILSLIB::Log::info("\t--eegrad val      radius of the scalp surface to use in EEG sphere model (default : {:7.1f} mm)", 1000 * eeg_sphere_rad);
    UTILSLIB::Log::info("\t--accurate        use accurate coil definitions in MEG forward computation");

    UTILSLIB::Log::info("\nFitting parameters:\n");
    UTILSLIB::Log::info("\t--guess name      The source space of initial guesses.");
    UTILSLIB::Log::info("\t                  If not present, the values below are used to generate the guess grid.");
    UTILSLIB::Log::info("\t--guesssurf name  Read the inner skull surface from this fif file to generate the guesses.");
    UTILSLIB::Log::info("\t--guessrad value  Radius of a spherical guess volume if neither of the above is present (default : {:.1f} mm)", 1000 * guess_rad);
    UTILSLIB::Log::info("\t--exclude dist/mm Exclude points which are closer than this distance from the CM of the inner skull surface (default = {:6.1f} mm).", 1000 * guess_exclude);
    UTILSLIB::Log::info("\t--mindist dist/mm Exclude points which are closer than this distance from the inner skull surface (default = {:6.1f} mm).", 1000 * guess_mindist);
    UTILSLIB::Log::info("\t--grid    dist/mm Source space grid size (default = {:6.1f} mm).", 1000 * guess_grid);
    UTILSLIB::Log::info("\t--magdip          Fit magnetic dipoles instead of current dipoles.");
    UTILSLIB::Log::info("\nOutput:\n");
    UTILSLIB::Log::info("\t--dip     name    xfit dip format output file name");
    UTILSLIB::Log::info("\t--bdip    name    xfit bdip format output file name");
    UTILSLIB::Log::info("\nGeneral:\n");
    UTILSLIB::Log::info("\t--gui             Enables the gui.");
    UTILSLIB::Log::info("\t--help            print this info.");
    UTILSLIB::Log::info("\t--version         print version info.");
}

//=============================================================================================================

bool InvDipoleFitSettings::check_unrecognized_args(int argc, char** argv)
{
    if (argc > 1) {
        QString args;
        for (int k = 1; k < argc; k++)
            args += QString(argv[k]) + " ";
        UTILSLIB::Log::critical("Unrecognized arguments : {}", args.trimmed().toStdString());
        return false;
    }
    return true;
}

//=============================================================================================================

bool InvDipoleFitSettings::check_args(int* argc, char** argv)
{
    int found;
    float fval;
    int ival, filter_size;

    for (int k = 0; k < *argc; k++) {
        found = 0;
        if (strcmp(argv[k], "--gui") == 0) {
            found = 1;
            gui = true;
        } else if (strcmp(argv[k], "--version") == 0) {
            UTILSLIB::Log::info("{} compiled at {} {}", argv[0], __DATE__, __TIME__);
            exit(0);
        } else if (strcmp(argv[k], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[k], "--guess") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--guess: argument required.");
                return false;
            }
            guessname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--gsurf") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--gsurf: argument required.");
                return false;
            }
            guess_surfname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--guesssurf") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--guesssurf: argument required.");
                return false;
            }
            guess_surfname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--guessrad") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--guessrad: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Could not interpret the radius.");
                return false;
            }
            if (fval <= 0.0) {
                UTILSLIB::Log::critical("Radius should be positive");
                return false;
            }
            guess_rad = fval / 1000.0;
        } else if (strcmp(argv[k], "--mindist") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--mindist: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Could not interpret the distance.");
                return false;
            }
            guess_mindist = fval / 1000.0;
            if (guess_mindist <= 0.0)
                guess_mindist = 0.0;
        } else if (strcmp(argv[k], "--exclude") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--exclude: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Could not interpret the distance.");
                return false;
            }
            guess_exclude = fval / 1000.0;
            if (guess_exclude <= 0.0)
                guess_exclude = 0.0;
        } else if (strcmp(argv[k], "--grid") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--grid: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Could not interpret the distance.");
                return false;
            }
            if (fval <= 0.0) {
                UTILSLIB::Log::critical("Grid spacing should be positive");
                return false;
            }
            // MNE-C divided the default instead of the value given, so --grid was ignored.
            guess_grid = fval / 1000.0;
        } else if (strcmp(argv[k], "--mri") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--mri: argument required.");
                return false;
            }
            mriname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--bem") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--bem: argument required.");
                return false;
            }
            bemname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--accurate") == 0) {
            found = 1;
            accurate = true;
        } else if (strcmp(argv[k], "--meg") == 0) {
            found = 1;
            include_meg = true;
        } else if (strcmp(argv[k], "--eeg") == 0) {
            found = 1;
            include_eeg = true;
        } else if (strcmp(argv[k], "--origin") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--origin: argument required.");
                return false;
            }
            if (!parseTriplet(argv[k + 1], r0)) {
                UTILSLIB::Log::critical("Could not interpret the origin.");
                return false;
            }
            r0[0] = r0[0] / 1000.0f;
            r0[1] = r0[1] / 1000.0f;
            r0[2] = r0[2] / 1000.0f;
        } else if (strcmp(argv[k], "--eegrad") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--eegrad: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], eeg_sphere_rad)) {
                UTILSLIB::Log::critical("Incomprehensible radius: {}", argv[k + 1]);
                return false;
            }
            if (eeg_sphere_rad <= 0) {
                UTILSLIB::Log::critical("Radius must be positive");
                return false;
            }
            eeg_sphere_rad = eeg_sphere_rad / 1000.0;
        } else if (strcmp(argv[k], "--eegmodels") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--eegmodels: argument required.");
                return false;
            }
            eeg_model_file = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--eegmodel") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--eegmodel: argument required.");
                return false;
            }
            eeg_model_name = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--eegscalp") == 0) {
            found = 1;
            scale_eeg_pos = true;
        } else if (strcmp(argv[k], "--meas") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--meas: argument required.");
                return false;
            }
            measname = QString(argv[k + 1]);
            is_raw = false;
        } else if (strcmp(argv[k], "--raw") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--raw: argument required.");
                return false;
            }
            measname = QString(argv[k + 1]);
            is_raw = true;
        } else if (strcmp(argv[k], "--proj") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--proj: argument required.");
                return false;
            }
            projnames.emplace_back(argv[k + 1]);
        } else if (strcmp(argv[k], "--noproj") == 0) {
            found = 1;
            omit_data_proj = true;
        } else if (strcmp(argv[k], "--bad") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--bad: argument required.");
                return false;
            }
            badname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--noise") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--noise: argument required.");
                return false;
            }
            noisename = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--gradnoise") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--gradnoise: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0.0) {
                UTILSLIB::Log::critical("Value should be positive");
                return false;
            }
            grad_std = 1e-13 * fval;
        } else if (strcmp(argv[k], "--magnoise") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--magnoise: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0.0) {
                UTILSLIB::Log::critical("Value should be positive");
                return false;
            }
            mag_std = 1e-15 * fval;
        } else if (strcmp(argv[k], "--eegnoise") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--eegnoise: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0.0) {
                UTILSLIB::Log::critical("Value should be positive");
                return false;
            }
            eeg_std = 1e-6 * fval;
        } else if (strcmp(argv[k], "--diagnoise") == 0) {
            found = 1;
            diagnoise = true;
        } else if (strcmp(argv[k], "--eegreg") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--eegreg: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0 || fval > 1) {
                UTILSLIB::Log::critical("Regularization value should be positive and smaller than one.");
                return false;
            }
            eeg_reg = fval;
        } else if (strcmp(argv[k], "--magreg") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--magreg: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0 || fval > 1) {
                UTILSLIB::Log::critical("Regularization value should be positive and smaller than one.");
                return false;
            }
            mag_reg = fval;
        } else if (strcmp(argv[k], "--gradreg") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--gradreg: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0 || fval > 1) {
                UTILSLIB::Log::critical("Regularization value should be positive and smaller than one.");
                return false;
            }
            grad_reg = fval;
        } else if (strcmp(argv[k], "--reg") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--reg: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible value: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0 || fval > 1) {
                UTILSLIB::Log::critical("Regularization value should be positive and smaller than one.");
                return false;
            }
            grad_reg = fval;
            mag_reg = fval;
            eeg_reg = fval;
        } else if (strcmp(argv[k], "--tstep") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--tstep: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible tstep: {}", argv[k + 1]);
                return false;
            }
            if (fval < 0.0) {
                UTILSLIB::Log::critical("Time step should be positive");
                return false;
            }
            tstep = fval / 1000.0;
        } else if (strcmp(argv[k], "--integ") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--integ: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible integration time: {}", argv[k + 1]);
                return false;
            }
            if (fval <= 0.0) {
                UTILSLIB::Log::critical("Integration time should be positive.");
                return false;
            }
            integ = fval / 1000.0f;
        } else if (strcmp(argv[k], "--tmin") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--tmin: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible tmin: {}", argv[k + 1]);
                return false;
            }
            tmin = fval / 1000.0f;
        } else if (strcmp(argv[k], "--tmax") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--tmax: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible tmax: {}", argv[k + 1]);
                return false;
            }
            tmax = fval / 1000.0;
        } else if (strcmp(argv[k], "--bmin") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--bmin: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible bmin: {}", argv[k + 1]);
                return false;
            }
            bmin = fval / 1000.0f;
        } else if (strcmp(argv[k], "--bmax") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--bmax: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Incomprehensible bmax: {}", argv[k + 1]);
                return false;
            }
            bmax = fval / 1000.0f;
        } else if (strcmp(argv[k], "--set") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--set: argument required.");
                return false;
            }
            if (!parseInt(argv[k + 1], setno)) {
                UTILSLIB::Log::critical("Incomprehensible data set number: {}", argv[k + 1]);
                return false;
            }
            if (setno <= 0) {
                UTILSLIB::Log::critical("Data set number must be > 0");
                return false;
            }
        } else if (strcmp(argv[k], "--filteroff") == 0) {
            found = 1;
            filter.filter_on = false;
        } else if (strcmp(argv[k], "--lowpass") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--lowpass: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Illegal number: {}", argv[k + 1]);
                return false;
            }
            if (fval <= 0) {
                UTILSLIB::Log::critical("Lowpass corner must be positive");
                return false;
            }
            filter.lowpass = fval;
        } else if (strcmp(argv[k], "--lowpassw") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--lowpassw: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Illegal number: {}", argv[k + 1]);
                return false;
            }
            if (fval <= 0) {
                UTILSLIB::Log::critical("Lowpass width must be positive");
                return false;
            }
            filter.lowpass_width = fval;
        } else if (strcmp(argv[k], "--highpass") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--highpass: argument required.");
                return false;
            }
            if (!parseFloat(argv[k + 1], fval)) {
                UTILSLIB::Log::critical("Illegal number: {}", argv[k + 1]);
                return false;
            }
            if (fval <= 0) {
                UTILSLIB::Log::critical("Highpass corner must be positive");
                return false;
            }
            filter.highpass = fval;
        } else if (strcmp(argv[k], "--filtersize") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--filtersize: argument required.");
                return false;
            }
            if (!parseInt(argv[k + 1], ival)) {
                UTILSLIB::Log::critical("Illegal number: {}", argv[k + 1]);
                return false;
            }
            if (ival < 1024) {
                UTILSLIB::Log::critical("Filtersize should be at least 1024.");
                return false;
            }
            for (filter_size = 1024; filter_size < ival; filter_size = 2 * filter_size)
                ;
            filter.size = filter_size;
            filter.taper_size = filter_size / 2;
        } else if (strcmp(argv[k], "--magdip") == 0) {
            found = 1;
            fit_mag_dipoles = true;
        } else if (strcmp(argv[k], "--dip") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--dip: argument required.");
                return false;
            }
            dipname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--bdip") == 0) {
            found = 2;
            if (k == *argc - 1) {
                UTILSLIB::Log::critical("--bdip: argument required.");
                return false;
            }
            bdipname = QString(argv[k + 1]);
        } else if (strcmp(argv[k], "--verbose") == 0) {
            found = 1;
            verbose = true;
        }
        if (found) {
            for (int p = k; p < *argc - found; p++)
                argv[p] = argv[p + found];
            *argc = *argc - found;
            k = k - found;
        }
    }
    return check_unrecognized_args(*argc, argv);
}
