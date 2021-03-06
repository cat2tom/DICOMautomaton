//Average.cc - A part of DICOMautomaton 2017. Written by hal clark.

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>    
#include <vector>
#include <set> 
#include <map>
#include <unordered_map>
#include <list>
#include <functional>
#include <thread>
#include <array>
#include <mutex>
#include <limits>
#include <cmath>
#include <regex>

#include <getopt.h>           //Needed for 'getopts' argument parsing.
#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.
#include <algorithm>
#include <experimental/optional>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathPlottingGnuplot.h" //Needed for YgorMathPlottingGnuplot::*.
#include "YgorMathChebyshev.h" //Needed for cheby_approx class.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorContainers.h"   //Needed for bimap class.
#include "YgorPerformance.h"  //Needed for YgorPerformance_dt_from_last().
#include "YgorAlgorithms.h"   //Needed for For_Each_In_Parallel<..>(...)
#include "YgorArguments.h"    //Needed for ArgumentHandler class.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "YgorImages.h"
#include "YgorImagesIO.h"
#include "YgorImagesPlotting.h"

#include "Explicator.h"       //Needed for Explicator class.

#include "../Structs.h"

#include "../YgorImages_Functors/Grouping/Misc_Functors.h"

#include "../YgorImages_Functors/Processing/Average_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/DCEMRI_AUC_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_S0_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_T1_Map.h"
#include "../YgorImages_Functors/Processing/Highlight_ROI_Voxels.h"
#include "../YgorImages_Functors/Processing/Kitchen_Sink_Analysis.h"
#include "../YgorImages_Functors/Processing/IVIMMRI_ADC_Map.h"
#include "../YgorImages_Functors/Processing/Time_Course_Slope_Map.h"
#include "../YgorImages_Functors/Processing/CT_Perfusion_Clip_Search.h"
#include "../YgorImages_Functors/Processing/CT_Perf_Pixel_Filter.h"
#include "../YgorImages_Functors/Processing/CT_Convert_NaNs_to_Air.h"
#include "../YgorImages_Functors/Processing/Min_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/Max_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/CT_Reasonable_HU_Window.h"
#include "../YgorImages_Functors/Processing/Slope_Difference.h"
#include "../YgorImages_Functors/Processing/Centralized_Moments.h"
#include "../YgorImages_Functors/Processing/Logarithmic_Pixel_Scale.h"
#include "../YgorImages_Functors/Processing/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Processing/DBSCAN_Time_Courses.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bilinear_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bicubic_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Pixel_Decimate.h"
#include "../YgorImages_Functors/Processing/Cross_Second_Derivative.h"
#include "../YgorImages_Functors/Processing/Orthogonal_Slices.h"

#include "../YgorImages_Functors/Transform/DCEMRI_C_Map.h"
#include "../YgorImages_Functors/Transform/DCEMRI_Signal_Difference_C.h"
#include "../YgorImages_Functors/Transform/CT_Perfusion_Signal_Diff.h"
#include "../YgorImages_Functors/Transform/DCEMRI_S0_Map_v2.h"
#include "../YgorImages_Functors/Transform/DCEMRI_T1_Map_v2.h"
#include "../YgorImages_Functors/Transform/Pixel_Value_Histogram.h"
#include "../YgorImages_Functors/Transform/Subtract_Spatially_Overlapping_Images.h"

#include "../YgorImages_Functors/Compute/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Compute/Contour_Similarity.h"

#include "Average.h"



std::list<OperationArgDoc> OpArgDocAverage(void){
    std::list<OperationArgDoc> out;

    // This operation averages image or dose volumes. It can average over spatial or temporal dimensions. However, rather than
    // relying specifically on time for temporal averaging, any images that have overlapping voxels can be averaged.
    //
    // This operation is typically used to create an aggregate view of a large volume of data. It may also increase SNR
    // and can be used for contouring purposes.
    //

    out.emplace_back();
    out.back().name = "DoseImageSelection";
    out.back().desc = "Dose images to operate on. Either 'none', 'last', or 'all'.";
    out.back().default_val = "none";
    out.back().expected = true;
    out.back().examples = { "none", "last", "all" };

    
    out.emplace_back();
    out.back().name = "ImageSelection";
    out.back().desc = "Images to operate on. Either 'none', 'last', or 'all'.";
    out.back().default_val = "last";
    out.back().expected = true;
    out.back().examples = { "none", "last", "all" };

   
    out.emplace_back();
    out.back().name = "AveragingMethod";
    out.back().desc = "The averaging method to use. Valid methods are 'overlapping-spatially' and 'overlapping-temporally'.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "overlapping-spatially", "overlapping-temporally" };

    return out;
}


Drover Average(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string /*FilenameLex*/){

    //---------------------------------------------- User Parameters --------------------------------------------------
    const auto DoseImageSelectionStr = OptArgs.getValueStr("DoseImageSelection").value();
    const auto ImageSelectionStr = OptArgs.getValueStr("ImageSelection").value();
    const auto AveragingMethodStr = OptArgs.getValueStr("AveragingMethod").value();
    //-----------------------------------------------------------------------------------------------------------------
    const auto regex_none = std::regex("no?n?e?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_last = std::regex("la?s?t?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_all  = std::regex("al?l?$",   std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto overlap_spat = std::regex("overlapping-spatially", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto overlap_temp = std::regex("overlapping-temporally", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);


    if( !std::regex_match(DoseImageSelectionStr, regex_none)
    &&  !std::regex_match(DoseImageSelectionStr, regex_last)
    &&  !std::regex_match(DoseImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Dose Image selection is not valid. Cannot continue.");
    }
    if( !std::regex_match(ImageSelectionStr, regex_none)
    &&  !std::regex_match(ImageSelectionStr, regex_last)
    &&  !std::regex_match(ImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Image selection is not valid. Cannot continue.");
    }
    
    if(false){
    }else if(std::regex_match(AveragingMethodStr, overlap_spat)){
        //Image data.
        auto iap_it = DICOM_data.image_data.begin();
        if(false){
        }else if(std::regex_match(ImageSelectionStr, regex_none)){ 
            iap_it = DICOM_data.image_data.end();
        }else if(std::regex_match(ImageSelectionStr, regex_last)){
            if(!DICOM_data.image_data.empty()) iap_it = std::prev(DICOM_data.image_data.end());
        }
        while(iap_it != DICOM_data.image_data.end()){
            if(!(*iap_it)->imagecoll.Process_Images_Parallel( GroupSpatiallyOverlappingImages,
                                                              CondenseAveragePixel,
                                                              {}, {} )){
                throw std::runtime_error("Unable to average (image_array, overlapping-spatially).");
            }
            ++iap_it;
        }

        //Dose data.
        auto dap_it = DICOM_data.dose_data.begin();
        if(false){
        }else if(std::regex_match(DoseImageSelectionStr, regex_none)){ 
            dap_it = DICOM_data.dose_data.end();
        }else if(std::regex_match(DoseImageSelectionStr, regex_last)){
            if(!DICOM_data.dose_data.empty()) dap_it = std::prev(DICOM_data.dose_data.end());
        }
        while(dap_it != DICOM_data.dose_data.end()){
            if(!(*dap_it)->imagecoll.Process_Images_Parallel( GroupSpatiallyOverlappingImages,
                                                              CondenseAveragePixel,
                                                              {}, {} )){
                throw std::runtime_error("Unable to average (dose_array, overlapping-spatially).");
            }
            ++dap_it;
        }

    }else if(std::regex_match(AveragingMethodStr, overlap_temp)){
        //Image data.
        auto iap_it = DICOM_data.image_data.begin();
        if(false){
        }else if(std::regex_match(ImageSelectionStr, regex_none)){ 
            iap_it = DICOM_data.image_data.end();
        }else if(std::regex_match(ImageSelectionStr, regex_last)){
            if(!DICOM_data.image_data.empty()) iap_it = std::prev(DICOM_data.image_data.end());
        }
        while(iap_it != DICOM_data.image_data.end()){
            if(!(*iap_it)->imagecoll.Process_Images_Parallel( GroupTemporallyOverlappingImages,
                                                              CondenseAveragePixel,
                                                              {}, {} )){
                throw std::runtime_error("Unable to average (image_array, overlapping-temporally).");
            }
            ++iap_it;
        }

        //Dose data.
        auto dap_it = DICOM_data.dose_data.begin();
        if(false){
        }else if(std::regex_match(DoseImageSelectionStr, regex_none)){ 
            dap_it = DICOM_data.dose_data.end();
        }else if(std::regex_match(DoseImageSelectionStr, regex_last)){
            if(!DICOM_data.dose_data.empty()) dap_it = std::prev(DICOM_data.dose_data.end());
        }
        while(dap_it != DICOM_data.dose_data.end()){
            if(!(*dap_it)->imagecoll.Process_Images_Parallel( GroupTemporallyOverlappingImages,
                                                              CondenseAveragePixel,
                                                              {}, {} )){
                throw std::runtime_error("Unable to average (dose_array, overlapping-temporally).");
            }
            ++dap_it;
        }


    }else{
        throw std::invalid_argument("Invalid averaging method specified. Cannot continue");
    }

    return std::move(DICOM_data);
}
