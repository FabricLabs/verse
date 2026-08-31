# Isometric Coordinate System Analysis

## 🔍 **Current Implementation Analysis**

Based on the code and screenshot, here's what we have:

### **Isometric Projection Formula:**
```c
// In isometric_world_to_screen():
*screen_x = screen_center_x + (rel_x - rel_z) * tile_width / 2;
*screen_y = screen_center_y + (rel_x + rel_z) * tile_height / 2 - rel_y * voxel_height;
```

### **Coordinate System:**
- **X axis**: Increases → right-down in screen space
- **Z axis**: Increases → left-down in screen space
- **Y axis**: Increases → up in screen space

### **Face Visibility Checks:**
```c
// Top face (y+1)
Voxel* above = world_get_voxel(world, x, y + 1, z);
visible_faces[0] = !above || above->type == VOXEL_AIR;

// Left face (x-1)
Voxel* left = world_get_voxel(world, x - 1, y, z);
visible_faces[2] = !left || left->type == VOXEL_AIR;

// Right face (x+1)
Voxel* right = world_get_voxel(world, x + 1, y, z);
visible_faces[3] = !right || right->type == VOXEL_AIR;

// Front face (z-1)
Voxel* front = world_get_voxel(world, x, y, z - 1);
visible_faces[4] = !front || front->type == VOXEL_AIR;

// Back face (z+1)
Voxel* back = world_get_voxel(world, x, y, z + 1);
visible_faces[5] = !back || back->type == VOXEL_AIR;
```

### **Rendered Faces:**
Only 3 faces are rendered in `isometric_renderer_draw_voxel()`:
- **Top face** (diamond, brightest)
- **Left face** (left side, darkest)
- **Right face** (right side, medium brightness)

## 🤔 **Potential Issues**

### **1. Missing Front/Back Faces:**
The current renderer only draws 3 faces, but in isometric view, we should see:
- Top face ✅
- Left face ✅
- Right face ✅
- Front face ❌ (not rendered)

### **2. Face Naming Convention:**
The naming might be confusing. In the screenshot:
- The "left face" appears to be the **front-left** surface
- The "right face" appears to be the **front-right** surface
- We're missing the **back faces**

### **3. Standard Isometric Convention:**
Typical isometric games show 3 faces per cube:
- **Top** (diamond)
- **Left** (parallelogram facing left)
- **Right** (parallelogram facing right)

Our implementation seems correct for this convention.

## 📸 **Screenshot Analysis**

Looking at the attached image:
- ✅ **Top faces** (diamond shapes) are rendered correctly
- ✅ **Side faces** (triangular/parallelogram shapes) show depth
- ✅ **Color shading** differentiates the faces (bright top, darker sides)
- ✅ **Player position** (yellow dot) is visible
- ✅ **Terrain looks natural** with grass, dirt, and stone

## 🎯 **Conclusion**

The coordinate system appears to be working **correctly** for standard isometric rendering. The "triangular" surfaces you're seeing are actually the proper **parallelogram side faces** of the isometric cubes, which appear triangular due to the diamond-shaped tops.

### **If adjustment is needed:**
The most likely issue would be **Z-axis direction**. Some systems use:
- Z+ = towards camera (into screen)
- Z+ = away from camera (out of screen)

Current system uses Z+ = left-down, which is correct for most isometric implementations.

### **Recommendation:**
The rendering looks **correct** for standard isometric projection. The "triangular" faces are the expected side faces of isometric cubes viewed from the standard 30° angle.
