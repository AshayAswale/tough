#include <tough_perception_common/PerceptionHelper.h>

namespace tough_perception
{
void generateOrganizedRGBDCloud(const cv::Mat &dispImage, const cv::Mat &colorImage, const Eigen::Matrix4d Qmat,
                                tough_perception::StereoPointCloudColor::Ptr &cloud)
{
    int width = dispImage.cols;
    int height = dispImage.rows;

    if (!cloud)
        cloud = tough_perception::StereoPointCloudColor::Ptr(new tough_perception::StereoPointCloudColor());

    cloud->resize(width * height);
    cloud->height = height;
    cloud->width = width;

    for (int u = 0; u < dispImage.rows; u++)
        for (int v = 0; v < dispImage.cols; v++)
        {
            const float &currentPixelDisp = dispImage.at<float>(cv::Point(v, u));
            if (currentPixelDisp == 0.0)
                continue;
            Eigen::Vector4d pixelCoordVec(v, u, currentPixelDisp, 1);
            pixelCoordVec = Qmat * pixelCoordVec;
            pixelCoordVec /= pixelCoordVec(3);

            tough_perception::StereoPointColor &pt = cloud->at(v, u);

            pt.x = pixelCoordVec(0);
            pt.y = pixelCoordVec(1);
            pt.z = pixelCoordVec(2);

            const cv::Vec3b &rgb = colorImage.at<cv::Vec3b>(cv::Point(v, u));
            pt.b = rgb.val[0];
            pt.g = rgb.val[1];
            pt.r = rgb.val[2];
        }
}
} // namespace tough_perception