using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using UnityEngine.Rendering;
#if UNITY_EDITOR
using UnityEditor;
#endif

// Unity 2021.3+. Run on the Unity main thread in the owning project.
public static class ProjectBunnySkinningEvidence
{
    [Serializable]
    private sealed class AssetIdentity
    {
        public string guid;
        public long fileId;
    }

    [Serializable]
    private sealed class Node
    {
        public int instanceId;
        public int parentInstanceId;
        public string path;
        public AssetIdentity prefabSource;
        public AssetIdentity originalPrefabSource;
    }

    [Serializable]
    private sealed class Slot
    {
        public int slot;
        public int transformInstanceId;
        public bool hasBindPose;
        public float[] bindPoseColumnMajor;
    }

    [Serializable]
    private sealed class Level
    {
        public int groupInstanceId;
        public string groupPath;
        public int lod;
        public float screenRelativeTransitionHeight;
    }

    [Serializable]
    private sealed class Submesh
    {
        public int submesh;
        public int indexStart;
        public int indexCount;
        public int baseVertex;
        public MeshTopology topology;
    }

    [Serializable]
    private sealed class MaterialSlot
    {
        public int slot;
        public int materialInstanceId;
        public string materialName;
        public string shaderName;
        public AssetIdentity materialAsset;
    }

    [Serializable]
    private sealed class RendererEntry
    {
        public int rendererInstanceId;
        public string rendererType;
        public string scenePath;
        public string sceneName;
        public int sceneHandle;
        public string rendererPath;
        public AssetIdentity prefabSource;
        public AssetIdentity originalPrefabSource;
        public bool active;
        public bool enabled;
        public Level[] lods;
        public MaterialSlot[] materials;
        public int animatorInstanceId;
        public string animatorPath;
        public int avatarInstanceId;
        public AssetIdentity avatarAsset;
        public int rigRootInstanceId;
        public string rigRootSource;
        public Node[] hierarchy;
        public int meshInstanceId;
        public string meshName;
        public AssetIdentity meshAsset;
        public int vertexCount;
        public IndexFormat indexFormat;
        public Submesh[] submeshes;
        public int bindPoseCount;
        public string bindPoseState;
        public Slot[] meshLocalSlots;
    }

    [Serializable]
    private sealed class Snapshot
    {
        public int schemaVersion = 1;
        public string unityVersion;
        public int processId;
        public long timestampFrequency;
        public int frame;
        public long timestamp;
        public RendererEntry[] renderers;
    }

    [Serializable]
    private sealed class PaletteObservation
    {
        public int schemaVersion = 1;
        public int processId;
        public int frame;
        public long timestampFrequency;
        public long beforeUpload;
        public long afterUpload;
        public string paletteRole;
        public string assemblyObservationId;
        public RendererEntry renderer;
        public int[] uploadedSlotToTransformInstanceId;
        public Node[] uploadedHierarchy;
        public int byteCount;
        public string crc32c;
    }

    [Serializable]
    private sealed class AppearanceObservation
    {
        public int schemaVersion = 1;
        public int processId;
        public string targetId;
        public int frame;
        public long timestampFrequency;
        public string observationId;
        public string scenario;
        public string assembler;
        public long beforeAssembly;
        public long afterAssembly;
        public int appearanceRootInstanceId;
        public string appearanceRootPath;
        public AssetIdentity appearanceSource;
        public RendererEntry[] renderers;
    }

    [Serializable]
    public sealed class PaletteEnqueueObservation
    {
        public string kind = "palette_enqueue";
        public int schemaVersion = 1;
        public string enqueueId;
        public string targetId;
        public string assemblyObservationId;
        public int rendererInstanceId;
        public string paletteRole;
        public long timestampFrequency;
        public string timestampClock;
        public int frame;
        public uint nativeThreadId;
        public int managedThreadId;
        public string[] managedCallerFrames;
        public string managedCallerState;
        public long beforeEnqueue;
        public long afterEnqueue;
        public int destinationByteOffset;
        public int byteCount;
        public string crc32c;
    }

    [Serializable]
    private sealed class CandidateMethod
    {
        public string assembly;
        public string type;
        public string method;
        public string signature;
    }

    [Serializable]
    private sealed class CandidateInventory
    {
        public string assemblyName;
        public string[] terms;
        public int assembliesFound;
        public bool incompleteTypeMetadata;
        public bool truncated;
        public CandidateMethod[] methods;
    }

    // Includes inactive renderers and every LOD level in loaded scenes.
    // Call again after a different scene or resource variant is loaded.
    public static void ExportLoadedRenderers(string path)
    {
        var lods = FindLodMembership();
        var entries = new List<RendererEntry>();
        foreach (var renderer in Resources.FindObjectsOfTypeAll<SkinnedMeshRenderer>())
        {
            if (InLoadedScene(renderer))
                entries.Add(DescribeRenderer(renderer, lods));
        }
        entries.Sort((a, b) => a.rendererInstanceId.CompareTo(b.rendererInstanceId));
        var snapshot = new Snapshot
        {
            unityVersion = Application.unityVersion,
            processId = ProcessId(),
            timestampFrequency = Stopwatch.Frequency,
            frame = Time.frameCount,
            timestamp = Stopwatch.GetTimestamp(),
            renderers = entries.ToArray()
        };
        File.WriteAllText(path, JsonUtility.ToJson(snapshot, true));
    }

    // Call after the owning packer uploads the supplied bytes. Take the two
    // timestamps immediately around its upload call with Stopwatch.GetTimestamp.
    // packedSlots must describe the final upload order, including empty slots.
    public static void AppendPaletteUpload(string path, SkinnedMeshRenderer renderer, string paletteRole,
        Transform[] packedSlots, byte[] uploadedBytes, long beforeUpload, long afterUpload,
        string assemblyObservationId = null)
    {
        if (!renderer) throw new ArgumentNullException(nameof(renderer));
        if (packedSlots == null) throw new ArgumentNullException(nameof(packedSlots));
        if (uploadedBytes == null) throw new ArgumentNullException(nameof(uploadedBytes));
        if (afterUpload < beforeUpload) throw new ArgumentException("Upload timestamps are reversed.");
        var slots = new int[packedSlots.Length];
        var hierarchy = new Dictionary<int, Node>();
        for (var i = 0; i < packedSlots.Length; i++)
        {
            slots[i] = InstanceId(packedSlots[i]);
            AddAncestors(packedSlots[i], hierarchy);
        }
        var row = new PaletteObservation
        {
            processId = ProcessId(),
            frame = Time.frameCount,
            timestampFrequency = Stopwatch.Frequency,
            beforeUpload = beforeUpload,
            afterUpload = afterUpload,
            paletteRole = paletteRole,
            assemblyObservationId = assemblyObservationId,
            renderer = DescribeRenderer(renderer, FindLodMembership()),
            uploadedSlotToTransformInstanceId = slots,
            uploadedHierarchy = Nodes(hierarchy),
            byteCount = uploadedBytes.Length,
            crc32c = Crc32c(uploadedBytes)
        };
        File.AppendAllText(path, JsonUtility.ToJson(row) + Environment.NewLine);
    }

    // Call at the app-owned submission site with the final upload bytes.
    // The caller can retain enqueueId in its own command metadata.
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static PaletteEnqueueObservation CapturePaletteEnqueue(SkinnedMeshRenderer renderer, string paletteRole,
        string assemblyObservationId, byte[] uploadBytes, int destinationByteOffset, int callerFrames = 12)
    {
        if (!renderer) throw new ArgumentNullException(nameof(renderer));
        if (uploadBytes == null || uploadBytes.Length == 0)
            throw new ArgumentException("Supply the exact nonempty upload payload.", nameof(uploadBytes));
        if (destinationByteOffset < 0) throw new ArgumentOutOfRangeException(nameof(destinationByteOffset));
        if (callerFrames < 0 || callerFrames > 16) throw new ArgumentOutOfRangeException(nameof(callerFrames));
        var state = "disabled";
        var frames = new List<string>(callerFrames);
        if (callerFrames > 0)
        {
            var stack = new StackTrace(1, false);
            for (var i = 0; i < Math.Min(stack.FrameCount, callerFrames); i++)
            {
                var method = stack.GetFrame(i)?.GetMethod();
                frames.Add(method == null ? null : method.DeclaringType?.FullName + "." + method);
            }
            state = frames.Count == 0 ? "unavailable" :
                stack.FrameCount > callerFrames || frames.Contains(null) ? "partial" : "captured";
        }
        return new PaletteEnqueueObservation
        {
            enqueueId = Guid.NewGuid().ToString("N"),
            targetId = TargetId(),
            assemblyObservationId = assemblyObservationId,
            rendererInstanceId = renderer.GetInstanceID(),
            paletteRole = paletteRole,
            timestampFrequency = PaletteEnqueueFrequency(),
            timestampClock = PaletteEnqueueClock(),
            frame = Time.frameCount,
            nativeThreadId = NativeThreadId(),
            managedThreadId = Thread.CurrentThread.ManagedThreadId,
            managedCallerFrames = frames.ToArray(),
            managedCallerState = state,
            destinationByteOffset = destinationByteOffset,
            byteCount = uploadBytes.Length,
            crc32c = Crc32c(uploadBytes)
        };
    }

    // Bracket the existing enqueue call with PaletteEnqueueTimestamp() and
    // append only after it returns. Logging stays outside that interval.
    public static void AppendPaletteEnqueue(string path, PaletteEnqueueObservation observation,
        long beforeEnqueue, long afterEnqueue)
    {
        if (observation == null) throw new ArgumentNullException(nameof(observation));
        if (beforeEnqueue <= 0 || afterEnqueue < beforeEnqueue)
            throw new ArgumentException("Supply ordered enqueue timestamps.");
        observation.beforeEnqueue = beforeEnqueue;
        observation.afterEnqueue = afterEnqueue;
        File.AppendAllText(path, JsonUtility.ToJson(observation) + Environment.NewLine);
    }

    public static long PaletteEnqueueTimestamp()
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        if (QueryPerformanceCounter(out var ticks)) return ticks;
        throw new InvalidOperationException("QueryPerformanceCounter failed.");
#else
        return Stopwatch.GetTimestamp();
#endif
    }

    // Call at an existing appearance-ready point in an owned development build.
    // The caller names the method it actually observed and supplies a unique
    // observation ID for downstream palette writes.
    public static void AppendAppearanceObservation(string path, string observationId, string scenario,
        string assembler, GameObject appearanceRoot, long beforeAssembly, long afterAssembly)
    {
        if (!appearanceRoot) throw new ArgumentNullException(nameof(appearanceRoot));
        if (string.IsNullOrWhiteSpace(observationId) || string.IsNullOrWhiteSpace(assembler))
            throw new ArgumentException("Supply an observation ID and the observed assembler identity.");
        if (afterAssembly < beforeAssembly) throw new ArgumentException("Assembly timestamps are reversed.");
        var lods = FindLodMembership();
        var renderers = new List<RendererEntry>();
        foreach (var renderer in appearanceRoot.GetComponentsInChildren<Renderer>(true))
        {
            if (renderer is SkinnedMeshRenderer || renderer is MeshRenderer)
                renderers.Add(DescribeRenderer(renderer, lods));
        }
        var row = new AppearanceObservation
        {
            processId = ProcessId(),
            targetId = TargetId(),
            frame = Time.frameCount,
            timestampFrequency = Stopwatch.Frequency,
            observationId = observationId,
            scenario = scenario,
            assembler = assembler,
            beforeAssembly = beforeAssembly,
            afterAssembly = afterAssembly,
            appearanceRootInstanceId = appearanceRoot.GetInstanceID(),
            appearanceRootPath = HierarchyPath(appearanceRoot.transform),
            appearanceSource = PrefabIdentity(appearanceRoot, false),
            renderers = renderers.ToArray()
        };
        File.AppendAllText(path, JsonUtility.ToJson(row) + Environment.NewLine);
    }

    // Enumerate retained metadata through managed reflection. No candidates
    // are invoked and no native method addresses are inferred.
    public static void ExportCandidateMethods(string path, string assemblyName, string[] terms)
    {
        if (string.IsNullOrWhiteSpace(assemblyName))
            throw new ArgumentException("Supply the simple name of a loaded project assembly.", nameof(assemblyName));
        if (terms == null || terms.Length == 0 || Array.Exists(terms, string.IsNullOrWhiteSpace))
            throw new ArgumentException("Supply nonempty type or method name fragments.", nameof(terms));
        var result = new CandidateInventory { assemblyName = assemblyName, terms = terms };
        var candidates = new List<CandidateMethod>();
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            if (!string.Equals(assembly.GetName().Name, assemblyName, StringComparison.Ordinal)) continue;
            result.assembliesFound++;
            AddCandidates(assembly, terms, result, candidates);
            if (result.truncated) break;
        }
        result.methods = candidates.ToArray();
        File.WriteAllText(path, JsonUtility.ToJson(result, true));
    }

#if UNITY_EDITOR
    [MenuItem("Tools/ProjectBunny/Export loaded skinning renderers")]
    private static void ExportFromMenu()
    {
        var path = EditorUtility.SaveFilePanel("Export loaded skinning renderers", "", "skinning-renderers", "json");
        if (!string.IsNullOrEmpty(path)) ExportLoadedRenderers(path);
    }
#endif

    private static Dictionary<int, List<Level>> FindLodMembership()
    {
        var result = new Dictionary<int, List<Level>>();
        foreach (var group in Resources.FindObjectsOfTypeAll<LODGroup>())
        {
            if (!InLoadedScene(group)) continue;
            var levels = group.GetLODs();
            for (var i = 0; i < levels.Length; i++)
            {
                foreach (var renderer in levels[i].renderers)
                {
                    if (!renderer) continue;
                    var id = renderer.GetInstanceID();
                    if (!result.TryGetValue(id, out var membership))
                        result.Add(id, membership = new List<Level>());
                    membership.Add(new Level
                    {
                        groupInstanceId = group.GetInstanceID(),
                        groupPath = HierarchyPath(group.transform),
                        lod = i,
                        screenRelativeTransitionHeight = levels[i].screenRelativeTransitionHeight
                    });
                }
            }
        }
        return result;
    }

    private static RendererEntry DescribeRenderer(Renderer renderer, Dictionary<int, List<Level>> lods)
    {
        var scene = renderer.gameObject.scene;
        var skinned = renderer as SkinnedMeshRenderer;
        var meshFilter = skinned ? null : renderer.GetComponent<MeshFilter>();
        var mesh = skinned ? skinned.sharedMesh : meshFilter ? meshFilter.sharedMesh : null;
        var bones = skinned ? skinned.bones : Array.Empty<Transform>();
        var skinRoot = skinned ? skinned.rootBone : null;
        var root = skinRoot ? skinRoot : skinned ? renderer.transform.root : null;
        var animator = renderer.GetComponentInParent<Animator>(true);
        if (!animator && root) animator = root.GetComponentInParent<Animator>(true);
        var hierarchy = new Dictionary<int, Node>();
        AddAncestors(renderer.transform, hierarchy);
        AddAncestors(root, hierarchy);
        var bindPoses = Array.Empty<Matrix4x4>();
        var bindPoseState = !skinned ? "not_skinned" : mesh ? "observed" : "no_mesh";
        if (skinned && mesh)
        {
            // Runtime import/readability settings can make bind poses unavailable.
            try
            {
                bindPoses = mesh.bindposes ?? Array.Empty<Matrix4x4>();
                if (bindPoses.Length == 0) bindPoseState = "empty";
                else if (bindPoses.Length != bones.Length) bindPoseState = "slot_count_differs";
            }
            catch (UnityException) { bindPoseState = "unavailable"; }
        }
        var slots = new Slot[bones.Length];
        for (var i = 0; i < bones.Length; i++)
        {
            AddAncestors(bones[i], hierarchy);
            slots[i] = new Slot
            {
                slot = i,
                transformInstanceId = InstanceId(bones[i]),
                hasBindPose = i < bindPoses.Length,
                bindPoseColumnMajor = i < bindPoses.Length ? MatrixFloats(bindPoses[i]) : null
            };
        }

        var submeshes = mesh ? new Submesh[mesh.subMeshCount] : Array.Empty<Submesh>();
        for (var i = 0; i < submeshes.Length; i++)
        {
            var descriptor = mesh.GetSubMesh(i);
            submeshes[i] = new Submesh
            {
                submesh = i, indexStart = descriptor.indexStart, indexCount = descriptor.indexCount,
                baseVertex = descriptor.baseVertex, topology = descriptor.topology
            };
        }

        var id = renderer.GetInstanceID();
        return new RendererEntry
        {
            rendererInstanceId = id,
            rendererType = renderer.GetType().Name,
            scenePath = scene.path,
            sceneName = scene.name,
            sceneHandle = scene.handle,
            rendererPath = HierarchyPath(renderer.transform),
            prefabSource = PrefabIdentity(renderer, false),
            originalPrefabSource = PrefabIdentity(renderer, true),
            active = renderer.gameObject.activeInHierarchy,
            enabled = renderer.enabled,
            lods = lods.TryGetValue(id, out var membership) ? membership.ToArray() : Array.Empty<Level>(),
            materials = MaterialSlots(renderer),
            animatorInstanceId = InstanceId(animator),
            animatorPath = animator ? HierarchyPath(animator.transform) : null,
            avatarInstanceId = animator ? InstanceId(animator.avatar) : 0,
            avatarAsset = animator ? Identity(animator.avatar) : null,
            rigRootInstanceId = InstanceId(root),
            rigRootSource = skinRoot ? "skinned_mesh_renderer" : root ? "scene_hierarchy_root" : "not_skinned",
            hierarchy = Nodes(hierarchy),
            meshInstanceId = InstanceId(mesh),
            meshName = mesh ? mesh.name : null,
            meshAsset = Identity(mesh),
            vertexCount = mesh ? mesh.vertexCount : 0,
            indexFormat = mesh ? mesh.indexFormat : IndexFormat.UInt16,
            submeshes = submeshes,
            bindPoseCount = bindPoses.Length,
            bindPoseState = bindPoseState,
            meshLocalSlots = slots
        };
    }

    private static MaterialSlot[] MaterialSlots(Renderer renderer)
    {
        var materials = renderer.sharedMaterials;
        var slots = new MaterialSlot[materials.Length];
        for (var i = 0; i < slots.Length; i++)
        {
            var material = materials[i];
            slots[i] = new MaterialSlot
            {
                slot = i,
                materialInstanceId = InstanceId(material),
                materialName = material ? material.name : null,
                shaderName = material && material.shader ? material.shader.name : null,
                materialAsset = Identity(material)
            };
        }
        return slots;
    }

    private static void AddCandidates(Assembly assembly, string[] terms, CandidateInventory result,
        List<CandidateMethod> candidates)
    {
        Type[] types;
        try { types = assembly.GetTypes(); }
        catch (ReflectionTypeLoadException error)
        {
            types = error.Types;
            result.incompleteTypeMetadata = true;
        }
        const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic |
            BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        foreach (var type in types)
        {
            if (type == null) continue;
            try
            {
                var matchType = HasTerm(type.FullName ?? type.Name, terms);
                foreach (var method in type.GetMethods(flags))
                {
                    if (!matchType && !HasTerm(method.Name, terms)) continue;
                    if (candidates.Count == 256)
                    {
                        result.truncated = true;
                        return;
                    }
                    candidates.Add(new CandidateMethod
                    {
                        assembly = assembly.FullName,
                        type = type.FullName,
                        method = method.Name,
                        signature = method.ToString()
                    });
                }
            }
            catch (Exception error) when (error is TypeLoadException || error is FileNotFoundException)
            {
                result.incompleteTypeMetadata = true;
            }
        }
    }

    private static bool HasTerm(string value, string[] terms)
    {
        foreach (var term in terms)
            if (value.IndexOf(term, StringComparison.OrdinalIgnoreCase) >= 0) return true;
        return false;
    }

    private static bool InLoadedScene(Component component)
    {
        var scene = component.gameObject.scene;
        return scene.IsValid() && scene.isLoaded;
    }

    private static int InstanceId(UnityEngine.Object value) => value ? value.GetInstanceID() : 0;

    private static int ProcessId()
    {
        using (var process = Process.GetCurrentProcess()) return process.Id;
    }

    private static string TargetId()
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        if (!GetProcessTimes(GetCurrentProcess(), out var created, out _, out _, out _))
            throw new InvalidOperationException("GetProcessTimes failed.");
        return FormattableString.Invariant($"{GetCurrentProcessId()}:{created:x16}");
#else
        return null;
#endif
    }

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentProcessId();

    [DllImport("kernel32.dll")]
    private static extern bool GetProcessTimes(IntPtr process, out long creation, out long exit,
        out long kernel, out long user);

    [DllImport("kernel32.dll")]
    private static extern bool QueryPerformanceCounter(out long ticks);

    [DllImport("kernel32.dll")]
    private static extern bool QueryPerformanceFrequency(out long ticksPerSecond);
#endif

    private static long PaletteEnqueueFrequency()
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        if (QueryPerformanceFrequency(out var frequency)) return frequency;
        throw new InvalidOperationException("QueryPerformanceFrequency failed.");
#else
        return Stopwatch.Frequency;
#endif
    }

    private static string PaletteEnqueueClock()
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        return "windows_qpc";
#else
        return "stopwatch";
#endif
    }

    private static uint NativeThreadId()
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        return GetCurrentThreadId();
#else
        return 0;
#endif
    }

    private static void AddAncestors(Transform transform, Dictionary<int, Node> nodes)
    {
        for (var current = transform; current; current = current.parent)
        {
            var id = current.GetInstanceID();
            if (nodes.ContainsKey(id)) break;
            nodes.Add(id, new Node
            {
                instanceId = id,
                parentInstanceId = InstanceId(current.parent),
                path = HierarchyPath(current),
                prefabSource = PrefabIdentity(current, false),
                originalPrefabSource = PrefabIdentity(current, true)
            });
        }
    }

    private static Node[] Nodes(Dictionary<int, Node> nodes)
    {
        var result = new List<Node>(nodes.Values);
        result.Sort((a, b) => a.instanceId.CompareTo(b.instanceId));
        return result.ToArray();
    }

    private static string HierarchyPath(Transform transform)
    {
        var path = new Stack<string>();
        for (var current = transform; current; current = current.parent)
            path.Push(Uri.EscapeDataString(current.name) + "[" + current.GetSiblingIndex() + "]");
        return "/" + string.Join("/", path);
    }

    private static float[] MatrixFloats(Matrix4x4 matrix)
    {
        var values = new float[16];
        for (var column = 0; column < 4; column++)
            for (var row = 0; row < 4; row++)
                values[column * 4 + row] = matrix[row, column];
        return values;
    }

    private static AssetIdentity Identity(UnityEngine.Object value)
    {
#if UNITY_EDITOR
        if (value && AssetDatabase.TryGetGUIDAndLocalFileIdentifier(value, out string guid, out long fileId))
            return new AssetIdentity { guid = guid, fileId = fileId };
#endif
        return null;
    }

    private static AssetIdentity PrefabIdentity(UnityEngine.Object value, bool original)
    {
#if UNITY_EDITOR
        var source = original ? PrefabUtility.GetCorrespondingObjectFromOriginalSource(value)
            : PrefabUtility.GetCorrespondingObjectFromSource(value);
        return Identity(source);
#else
        return null;
#endif
    }

    private static string Crc32c(byte[] bytes)
    {
        uint crc = uint.MaxValue;
        foreach (var value in bytes)
        {
            crc ^= value;
            for (var i = 0; i < 8; i++)
                crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0x82f63b78u : 0);
        }
        return (~crc).ToString("x8");
    }
}
