/// \file OrbitalImageCrop.h
///
/// Orbital Image Crop class
///
/// TODO: Write something here

#ifndef __MVP_ORBITALIMAGECROP_H__
#define __MVP_ORBITALIMAGECROP_H__

#include <mvp/Config.h>
#include <mvp/OrbitalImageFileDescriptor.pb.h>

#include <vw/Image/ImageView.h>
#include <vw/Image/ImageViewRef.h>
#include <vw/Image/Algorithms.h>
#include <vw/Image/MaskViews.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/Camera/PinholeModel.h>
#include <vw/Cartography/Datum.h>
#include <vw/Cartography/SimplePointImageManipulation.h>

#include <boost/foreach.hpp>

#if MVP_ENABLE_OCTAVE_SUPPORT
#include <vw/Octave/Conversions.h>
#endif

// TODO: These functions should be in vw::camera
namespace vw {
namespace camera {
PinholeModel crop(PinholeModel const& cam, int32 x, int32 y, int32 width = 0, int32 height = 0) {
  PinholeModel result(cam);
  result.set_point_offset(result.point_offset() - Vector2(x, y));
  return result;
}
PinholeModel crop(PinholeModel const& cam, BBox2i const& bbox) {
  return crop(cam, bbox.min().x(), bbox.min().y(), bbox.width(), bbox.height());
}
}} // vw::camera

namespace mvp {

class OrbitalImageCrop : public vw::ImageView<vw::PixelMask<vw::PixelGray<vw::float32> > > {
  vw::camera::PinholeModel m_camera;

  static vw::ImageViewRef<vw::PixelMask<vw::PixelGray<vw::float32> > > rsrc_helper(boost::shared_ptr<vw::DiskImageResource> rsrc) {
    switch(rsrc->format().pixel_format) {
      case vw::VW_PIXEL_GRAYA:
        return vw::DiskImageView<vw::PixelMask<vw::PixelGray<vw::float32> > >(rsrc);
        break;
      case vw::VW_PIXEL_GRAY:
        return vw::pixel_cast<vw::PixelMask<vw::PixelGray<vw::float32> > >(vw::DiskImageView<vw::PixelGray<vw::float32> >(rsrc));
        break;
      default:
        vw::vw_throw(vw::ArgumentErr() << "Unsupported orbital image pixel format: " << vw::pixel_format_name(rsrc->format().pixel_format));
    }
  }
  public:
    static OrbitalImageCrop construct_from_paths(std::string const& image_path,
                                                 std::string const& camera_path) {
      boost::shared_ptr<vw::DiskImageResource> rsrc(vw::DiskImageResource::open(image_path));
      return OrbitalImageCrop(rsrc_helper(rsrc), vw::camera::PinholeModel(camera_path));
    }

    static OrbitalImageCrop construct_from_paths(std::string const& image_path,
                                                 std::string const& camera_path,
                                                 vw::BBox2 const& lonlat_bbox,
                                                 vw::cartography::Datum const& datum,
                                                 vw::Vector2 const& alt_limits) {

      boost::shared_ptr<vw::DiskImageResource> rsrc(vw::DiskImageResource::open(image_path));

      VW_ASSERT(datum.semi_major_axis() == datum.semi_minor_axis(), vw::LogicErr() << "Spheroid datums not supported");
      vw::Vector2 radius_range(alt_limits + vw::Vector2(datum.semi_major_axis(), datum.semi_major_axis()));

      vw::camera::PinholeModel camera(camera_path);

      std::vector<vw::Vector3> llr_bound_pts(8);
      llr_bound_pts[0] = vw::Vector3(lonlat_bbox.min()[0], lonlat_bbox.min()[1], radius_range[0]);
      llr_bound_pts[1] = vw::Vector3(lonlat_bbox.min()[0], lonlat_bbox.max()[1], radius_range[0]);
      llr_bound_pts[2] = vw::Vector3(lonlat_bbox.max()[0], lonlat_bbox.max()[1], radius_range[0]);
      llr_bound_pts[3] = vw::Vector3(lonlat_bbox.max()[0], lonlat_bbox.min()[1], radius_range[0]);
      llr_bound_pts[4] = vw::Vector3(lonlat_bbox.min()[0], lonlat_bbox.min()[1], radius_range[1]);
      llr_bound_pts[5] = vw::Vector3(lonlat_bbox.min()[0], lonlat_bbox.max()[1], radius_range[1]);
      llr_bound_pts[6] = vw::Vector3(lonlat_bbox.max()[0], lonlat_bbox.max()[1], radius_range[1]);
      llr_bound_pts[7] = vw::Vector3(lonlat_bbox.max()[0], lonlat_bbox.min()[1], radius_range[1]);

      vw::BBox2 cropbox;
      BOOST_FOREACH(vw::Vector3 const& llr, llr_bound_pts) {
        vw::Vector3 xyz(vw::cartography::lon_lat_radius_to_xyz(llr));
        cropbox.grow(camera.point_to_pixel(xyz));
      }

      cropbox.crop(vw::BBox2i(0, 0, rsrc->cols(), rsrc->rows()));

      // Return empty if smaller than a pixel
      if (cropbox.width() < 1 || cropbox.height() < 1) {
        return OrbitalImageCrop();
      }

      cropbox = vw::grow_bbox_to_int(cropbox);

      return OrbitalImageCrop(vw::crop(rsrc_helper(rsrc), cropbox), vw::camera::crop(camera, cropbox));
    }

    vw::camera::PinholeModel camera() const {return m_camera;}

  protected:
    // Make sure the user doesn't construct one like this
    OrbitalImageCrop() : vw::ImageView<vw::PixelMask<vw::PixelGray<vw::float32> > >(), m_camera() {}

    template <class ViewT>
    OrbitalImageCrop(vw::ImageViewBase<ViewT> const& image, vw::camera::PinholeModel camera) : 
      vw::ImageView<vw::PixelMask<vw::PixelGray<vw::float32> > >(image.impl()), m_camera(camera) {}
};

class OrbitalImageCropCollection : public std::vector<OrbitalImageCrop> {

  vw::BBox2 m_lonlat_bbox;
  vw::cartography::Datum m_datum;
  vw::Vector2 m_alt_limits;

  public:

    // This constructor constructs an OrbitalImageCropCollection that doesn't crop the images  
    OrbitalImageCropCollection() : m_lonlat_bbox(), m_datum(), m_alt_limits() {} 

    OrbitalImageCropCollection(vw::BBox2 const& lonlat_bbox, vw::cartography::Datum const& datum, vw::Vector2 const& alt_limits) : 
      m_lonlat_bbox(lonlat_bbox), m_datum(datum), m_alt_limits(alt_limits) {
      VW_ASSERT(m_datum.semi_major_axis() == m_datum.semi_minor_axis(), vw::LogicErr() << "Spheroid datums not supported");
    }

    void add_image(OrbitalImageFileDescriptor const& image_file) {
      add_image(image_file.image_path(), image_file.camera_path());
    }

    void add_image(std::string const& image_path, std::string const& camera_path) {
      if (m_lonlat_bbox.empty()) {
        // If the lonlat bbox is empty, we don't crop the images
        push_back(OrbitalImageCrop::construct_from_paths(image_path, camera_path));
      } else {
        OrbitalImageCrop image(OrbitalImageCrop::construct_from_paths(image_path, camera_path, m_lonlat_bbox, m_datum, m_alt_limits));
        if (image.cols() > 0 && image.rows() > 0) {
          push_back(image);
        }
      }
    }
    
    template <class CollectionT>
    void add_image_collection(CollectionT const& orbital_images) {
      BOOST_FOREACH(OrbitalImageFileDescriptor const& o, orbital_images) {
        add_image(o);
      }
    }

    #if MVP_ENABLE_OCTAVE_SUPPORT
    ::octave_map to_octave() const {
      ::octave_value_list datas;
      ::octave_value_list cameras;

      BOOST_FOREACH(OrbitalImageCrop const& o, *this) {
        datas.append(vw::octave::imageview_to_octave(o));
        cameras.append(vw::octave::pinhole_to_octave(o.camera()));
      }

      ::octave_map result;
      result.assign("data", datas);
      result.assign("camera", cameras);
      return result;
    }
    #endif
};

} // namespace mvp

#endif
