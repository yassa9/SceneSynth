import os
from transformers import AutoImageProcessor, GLPNForDepthEstimation
import torch
import numpy as np
from PIL import Image
import cv2

def estimate_depth_and_save_image(image_path):
    # Change directory to ../py_assets
    os.chdir('../py_assets')

    # Load the image
    image = Image.open(image_path)

    # Load the model and processor
    image_processor = AutoImageProcessor.from_pretrained("vinvino02/glpn-kitti")
    model = GLPNForDepthEstimation.from_pretrained("vinvino02/glpn-kitti")

    # Prepare image for the model
    inputs = image_processor(images=image, return_tensors="pt")

    # Perform depth estimation
    with torch.no_grad():
        outputs = model(**inputs)
        predicted_depth = outputs.predicted_depth

    # Interpolate to the original size
    prediction = torch.nn.functional.interpolate(
        predicted_depth.unsqueeze(1),
        size=image.size[::-1],
        mode="bicubic",
        align_corners=False,
    )

    # Process the prediction to create a depth map
    output = prediction.squeeze().cpu().numpy()
    formatted = (output * 255 / np.max(output)).astype("uint8")
    depth_colormap = cv2.applyColorMap(formatted, cv2.COLORMAP_INFERNO)

    # Define the output directory and create it if it doesn't exist
    output_dir = os.path.join(os.path.dirname(image_path), 'outputs')
    os.makedirs(output_dir, exist_ok=True)

    # Define the output path with the same name as the input image
    output_path = os.path.join(output_dir, os.path.basename(image_path))

    # Save the depth map in the 'outputs' directory
    cv2.imwrite(output_path, depth_colormap)

# Example usage
if __name__ == "__main__":
    image_path = '/home/yassa/repos/FlorenceFlame/py_assets/car.jpg'
    estimate_depth_and_save_image(image_path)
