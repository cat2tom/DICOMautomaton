//ContourWholeImages.cc - A part of DICOMautomaton 2017. Written by hal clark.

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
#include "../Thread_Pool.h"

#include "../YgorImages_Functors/Grouping/Misc_Functors.h"

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
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"

#include "ContourWholeImages.h"



std::list<OperationArgDoc> OpArgDocContourWholeImages(void){
    std::list<OperationArgDoc> out;

    // This operation constructs contours for an ROI that encompasses the whole of all specified images.
    // It is useful for operations that operate on ROIs whenever you want to compute something over the whole image.
    // This routine avoids having to manually contour anything.
    // The output is 'ephemeral' and is not commited to any database.
    //
    // NOTE: This routine will attempt to avoid repeat contours. Generated contours are tested for intersection with an
    //       image before the image is processed. 
    //
    // NOTE: Existing contours are ignored and unaltered.
    //
    // NOTE: Contours are set slightly inside the outer boundary so they can be easily visualized by overlaying on the
    //       image. All voxel centres will be within the bounds.
    //

    out.emplace_back();
    out.back().name = "ROILabel";
    out.back().desc = "A label to attach to the ROI contours.";
    out.back().default_val = "everything";
    out.back().expected = true;
    out.back().examples = { "everything", "whole_images", "unspecified" };

    
    out.emplace_back();
    out.back().name = "ImageSelection";
    out.back().desc = "Image collection to operate on. Either 'none', 'last', or 'all'.";
    out.back().default_val = "last";
    out.back().expected = true;
    out.back().examples = { "none", "last", "all" };

    return out;
}



Drover ContourWholeImages(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    //---------------------------------------------- User Parameters --------------------------------------------------
    const auto ROILabel = OptArgs.getValueStr("ROILabel").value();
    const auto ImageSelectionStr = OptArgs.getValueStr("ImageSelection").value();

    //-----------------------------------------------------------------------------------------------------------------

    const auto regex_none = std::regex("no?n?e?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_last = std::regex("la?s?t?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_all  = std::regex("al?l?$",   std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    if( !std::regex_match(ImageSelectionStr, regex_none)
    &&  !std::regex_match(ImageSelectionStr, regex_last)
    &&  !std::regex_match(ImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Image selection is not valid. Cannot continue.");
    }

    //Construct a destination for the ROI contours.
    if(DICOM_data.contour_data == nullptr){
        std::unique_ptr<Contour_Data> output (new Contour_Data());
        DICOM_data.contour_data = std::move(output);
    }
    DICOM_data.contour_data->ccs.emplace_back();

    DICOM_data.contour_data->ccs.back().Raw_ROI_name = ROILabel;
    DICOM_data.contour_data->ccs.back().ROI_number = 10001; // TODO: find highest existing and ++ it.
    DICOM_data.contour_data->ccs.back().Minimum_Separation = 1.0; // TODO: is there a routine to do this? (YES: Unique_Contour_Planes()...)

    //Iterate over each requested image_array. Each image is processed independently, so a thread pool is used.
    auto iap_it = DICOM_data.image_data.begin();
    if(false){
    }else if(std::regex_match(ImageSelectionStr, regex_none)){ iap_it = DICOM_data.image_data.end();
    }else if(std::regex_match(ImageSelectionStr, regex_last)){
        if(!DICOM_data.image_data.empty()) iap_it = std::prev(DICOM_data.image_data.end());
    }
    for( ; iap_it != DICOM_data.image_data.end(); ++iap_it){
        asio_thread_pool tp;
        std::mutex saver_printer; // Who gets to save generated contours, print to the console, and iterate the counter.
        long int completed = 0;
        const long int img_count = (*iap_it)->imagecoll.images.size();

        for(const auto &animg : (*iap_it)->imagecoll.images){
            if( (animg.rows < 1) || (animg.columns < 1) ){
                throw std::runtime_error("Image is empty -- cannot contour whole image.");
            }

            tp.submit_task([&](void) -> void {
                const auto R = animg.rows;
                const auto C = animg.columns;

                //Pin vertices to each corner of the image.
                const auto corner_A = animg.position(0,0)     - animg.row_unit*animg.pxl_dx*0.45 - animg.col_unit*animg.pxl_dy*0.45;
                const auto corner_B = animg.position(R-1,0)   + animg.row_unit*animg.pxl_dx*0.45 - animg.col_unit*animg.pxl_dy*0.45;
                const auto corner_C = animg.position(R-1,C-1) + animg.row_unit*animg.pxl_dx*0.45 + animg.col_unit*animg.pxl_dy*0.45;
                const auto corner_D = animg.position(0,C-1)   - animg.row_unit*animg.pxl_dx*0.45 + animg.col_unit*animg.pxl_dy*0.45;
                
                //Walk all available half-edges forming contour perimeters.
                std::list<contour_of_points<double>> cop;

                cop.emplace_back();
                cop.back().closed = true;
                cop.back().metadata["ROIName"] = ROILabel;
                cop.back().metadata["Description"] = "Whole-Image Contour";
                for(const auto &key : { "StudyInstanceUID", "FrameofReferenceUID" }){
                    if(animg.metadata.count(key) != 0) cop.back().metadata[key] = animg.metadata.at(key);
                }

                cop.back().points.emplace_back(corner_A);
                cop.back().points.emplace_back(corner_B);
                cop.back().points.emplace_back(corner_C);
                cop.back().points.emplace_back(corner_D);


                //Save the contours and print some information to screen.
                {
                    std::lock_guard<std::mutex> lock(saver_printer);

                    //Check if there is already a suitable contour. If so, do not bother saving the duplicate.
                    if(! animg.encompasses_any_part_of_contour_in_collection( DICOM_data.contour_data->ccs.back() )){
                        DICOM_data.contour_data->ccs.back().contours.splice( DICOM_data.contour_data->ccs.back().contours.end(), cop);
                    }    

                    ++completed;
                    FUNCINFO("Completed " << completed << " of " << img_count
                          << " --> " << static_cast<int>(1000.0*(completed)/img_count)/10.0 << "\% done");
                }
            }); // thread pool task closure.
        }
    }

    return std::move(DICOM_data);
}
