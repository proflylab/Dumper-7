#include "PackageManager.h"
#include "ObjectArray.h"

void FindCycle(const FindCycleParams& Params)
{
	FindCycleParams NewParams = {
		.Nodes = Params.Nodes,
		.PrevNode = Params.Requriements.PackageIdx,
		/* Requriements */
		.VisitedNodes = Params.VisitedNodes,
	};

	static auto FindCycleHandler = [&NewParams](const DependencyListType& Dependencies, VisitedNodeContainerType& VisitedNodes, int32 CurrentIndex, int32 PrevIndex,
		bool& bIsIncluded, bool bShouldHandlePackage, bool bIsStruct) -> void
	{
		if (!bShouldHandlePackage)
			return;

		if (!bIsIncluded)
		{
			bIsIncluded = true;

			IncludeStatus Status = { bIsIncluded && bIsStruct, bIsIncluded && !bIsStruct };

			VisitedNodes.push_back({ CurrentIndex, Status });

			for (auto& [Index, bShouldInclude] : Dependencies)
			{
				NewParams.bWasPrevNodeStructs = bIsStruct;
				NewParams.Requriements = { bIsStruct, !bIsStruct };
				NewParams.Requriements.PackageIdx = Index;
				FindCycle(NewParams);
			}
		}
		else if (bIsIncluded)
		{
			const bool bShouldIncludeStructs = bIsStruct;
			const bool bShouldIncludeClasses = !bIsStruct;

			auto CompareInfoPairs = [&](const VisitedNodeInformation& Info)
			{
				return Info.PackageIdx == CurrentIndex && ((Info.bIsIncluded.Structs && bShouldIncludeStructs) || (Info.bIsIncluded.Classes && bShouldIncludeClasses)); /* Maybe wrong */
			};

			/* No need to check unvisited nodes, they are guaranteed not to be in our "Visited" list */
			if (std::find_if(VisitedNodes.begin(), VisitedNodes.end(), CompareInfoPairs) != std::end(VisitedNodes))
			{
				//std::cout << "Found: " << Node << "\n";
				//wprintf(L"Cycle between: %d and %d\n", CurrentIndex, PrevIndex);
				std::cout << std::format("Cycle between \"{}_{}.hpp\" and \"{}_{}.hpp\"\n",
					ObjectArray::GetByIndex(PrevIndex).GetName(),
					NewParams.bWasPrevNodeStructs ? "structs" : "classes",
					ObjectArray::GetByIndex(CurrentIndex).GetName(),
					bIsStruct ? "structs" : "classes");

				auto& PackDeps = PackageManager::GetInfo(ObjectArray::GetByIndex(PrevIndex)).GetPackageDependencies();
				auto& SpecialDeps = bIsStruct ? PackDeps.StructsDependencies : PackDeps.ClassesDependencies;
				if (SpecialDeps.size() > 0x20)
					return;

				for (UEObject Obj : ObjectArray())
				{
					if (Obj.IsA(EClassCastFlags::Struct) && Obj.GetPackageIndex() == PrevIndex)
					{
						std::unordered_set<int32> Dependencies = PackageManagerUtils::GetDependencies(Obj.Cast<UEStruct>(), Obj.GetIndex());

						for (int32 Index : Dependencies)
						{
							UEObject Obj = ObjectArray::GetByIndex(Index);
							std::cout << std::format("Requires struct \"{}\" from \"{}\"\n", Obj.GetCppName(), Obj.GetOutermost().GetValidName());
						}
					}
				}
			}
		}
	};

	DependencyInfo& Dependencies = Params.Nodes[Params.Requriements.PackageIdx].PackageDependencies;

	FindCycleHandler(Dependencies.StructsDependencies, Params.VisitedNodes, Params.Requriements.PackageIdx, Params.PrevNode, Dependencies.bIsIncluded.Structs, Params.Requriements.bShouldInclude.Structs, true);
	FindCycleHandler(Dependencies.ClassesDependencies, Params.VisitedNodes, Params.Requriements.PackageIdx, Params.PrevNode, Dependencies.bIsIncluded.Classes, Params.Requriements.bShouldInclude.Structs, false);
}


PackageInfoHandle::PackageInfoHandle(const PackageInfo& InInfo)
	: Info(&InInfo)
{
}


const StringEntry& PackageInfoHandle::GetName() const
{
	return PackageManager::GetPackageName(*Info);
}


bool PackageInfoHandle::HasClasses() const
{
	return Info->bHasClasses;
}

bool PackageInfoHandle::HasStructs() const
{
	return Info->bHasStructs;
}

bool PackageInfoHandle::HasFunctions() const
{
	return Info->bHasFunctions;
}

bool PackageInfoHandle::HasParameterStructs() const
{
	return Info->bHasParams;
}

bool PackageInfoHandle::HasEnums() const
{
	return Info->bHasEnums;
}


const DependencyManager& PackageInfoHandle::GetSortedStructs() const
{
	return Info->StructsSorted;
}
const DependencyManager& PackageInfoHandle::GetSortedClasses() const
{
	return Info->ClassesSorted;
}

const std::vector<int32>& PackageInfoHandle::GetFunctions() const
{
	return Info->Functions;
}

const std::vector<int32>& PackageInfoHandle::GetEnums() const
{
	return Info->Enums;
}

const DependencyInfo& PackageInfoHandle::GetPackageDependencies() const
{
	return Info->PackageDependencies;
}


namespace PackageManagerUtils
{
	void GetPropertyDependency(UEProperty Prop, std::unordered_set<int32>& Store)
	{
		if (Prop.IsA(EClassCastFlags::StructProperty))
		{
			Store.insert(Prop.Cast<UEStructProperty>().GetUnderlayingStruct().GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::EnumProperty))
		{
			if (auto Enum = Prop.Cast<UEEnumProperty>().GetEnum())
				Store.insert(Enum.GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::ByteProperty))
		{
			if (UEObject Enum = Prop.Cast<UEByteProperty>().GetEnum())
				Store.insert(Enum.GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::ArrayProperty))
		{
			GetPropertyDependency(Prop.Cast<UEArrayProperty>().GetInnerProperty(), Store);
		}
		else if (Prop.IsA(EClassCastFlags::SetProperty))
		{
			GetPropertyDependency(Prop.Cast<UESetProperty>().GetElementProperty(), Store);
		}
		else if (Prop.IsA(EClassCastFlags::MapProperty))
		{
			GetPropertyDependency(Prop.Cast<UEMapProperty>().GetKeyProperty(), Store);
			GetPropertyDependency(Prop.Cast<UEMapProperty>().GetValueProperty(), Store);
		}
	}

	std::unordered_set<int32> GetDependencies(UEStruct Struct, int32 StructIndex)
	{
		std::unordered_set<int32> Dependencies;

		const int32 StructIdx = Struct.GetIndex();

		for (UEProperty Property : Struct.GetProperties())
		{
			GetPropertyDependency(Property, Dependencies);
		}

		Dependencies.erase(StructIdx);

		return Dependencies;
	};

	inline void SetPackageDependencies(DependencyListType& DependencyTracker, const std::unordered_set<int32>& Dependencies, int32 StructPackageIdx, bool bAllowToIncludeOwnPackage = false)
	{
		for (int32 Dependency : Dependencies)
		{
			const int32 PackageIdx = ObjectArray::GetByIndex(Dependency).GetPackageIndex();

			if (bAllowToIncludeOwnPackage || PackageIdx != StructPackageIdx)
				DependencyTracker[PackageIdx].bShouldInclude.Structs = true; // Dependencies only contains structs/enums which are in the "PackageName_structs.hpp" file
		}
	};

	inline void MoveDependenciesToDependencyManager(DependencyManager& StructDependencies, std::unordered_set<int32>&& DependeniesToMove, int32 StructIdx)
	{
		StructDependencies.SetDependencies(StructIdx, std::move(DependeniesToMove));
	};

	inline void BooleanOrEqual(bool& b1, bool b2)
	{
		b1 = b1 || b2;
	}
}

void PackageManager::InitNameAndDependencies()
{
	// Collects all packages required to compile this file
	
	for (auto Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject))
			continue;

		int32 CurrentPackageIdx = Obj.GetOutermost().GetIndex();

		const bool bIsStruct = Obj.IsA(EClassCastFlags::Struct);
		const bool bIsClass = Obj.IsA(EClassCastFlags::Class);

		const bool bIsFunction = Obj.IsA(EClassCastFlags::Function);
		const bool bIsEnum = Obj.IsA(EClassCastFlags::Function);

		const bool bIsPackage = Obj.IsA(EClassCastFlags::Package);

		if (bIsPackage)
		{
			PackageInfo& Info = PackageInfos[CurrentPackageIdx];
			Info.Name = UniquePackageNameTable.FindOrAdd(Obj.GetValidName()).first;
		}
		else if (bIsStruct && !bIsFunction)
		{
			PackageInfo& Info = PackageInfos[CurrentPackageIdx];

			UEStruct ObjAsStruct = Obj.Cast<UEStruct>();

			const int32 StructIdx = ObjAsStruct.GetIndex();
			const int32 StructPackageIdx = ObjAsStruct.GetPackageIndex();

			DependencyListType& PackageDependencyList = bIsClass ? Info.PackageDependencies.ClassesDependencies : Info.PackageDependencies.StructsDependencies;
			DependencyManager& ClassOrStructDependencyList = bIsClass ? Info.ClassesSorted : Info.StructsSorted;

			std::unordered_set<int32> Dependencies = PackageManagerUtils::GetDependencies(ObjAsStruct, StructIdx);

			PackageManagerUtils::SetPackageDependencies(PackageDependencyList, Dependencies, StructPackageIdx);
			PackageManagerUtils::MoveDependenciesToDependencyManager(ClassOrStructDependencyList, std::move(Dependencies), StructIdx);

			/* for both struct and class */
			if (UEStruct Super = ObjAsStruct.GetSuper())
			{
				const int32 SuperPackageIdx = Super.GetPackageIndex();

				if (SuperPackageIdx == StructPackageIdx)
				{
					/* In-file sorting is only required if the super-class is inside of the same package */
					ClassOrStructDependencyList.AddDependency(Obj.GetIndex(), Super.GetIndex());
				}
				else
				{
					/* A package can't depend on itself, super of a structs will always be in _"structs" file, same for classes and "_classes" files */
					IncludeStatus& bShouldInclude = PackageDependencyList[SuperPackageIdx].bShouldInclude;
					PackageManagerUtils::BooleanOrEqual(bShouldInclude.Structs, !bIsClass);
					PackageManagerUtils::BooleanOrEqual(bShouldInclude.Classes, bIsClass);
				}
			}

			PackageManagerUtils::BooleanOrEqual(Info.bHasStructs, bIsStruct);
			PackageManagerUtils::BooleanOrEqual(Info.bHasClasses, bIsClass);

			if (!bIsClass)
				continue;
			
			/* Add class-functions to package */
			for (UEFunction Func : ObjAsStruct.GetFunctions())
			{
				Info.bHasFunctions = true;
				Info.Functions.push_back(Obj.GetIndex());

				std::unordered_set<int32> Dependencies = PackageManagerUtils::GetDependencies(ObjAsStruct, StructIdx);

				PackageManagerUtils::BooleanOrEqual(Info.bHasParams, Func.HasMembers());

				PackageManagerUtils::SetPackageDependencies(Info.PackageDependencies.ParametersDependencies, Dependencies, StructPackageIdx, true);
			}
		}
		else if (bIsEnum)
		{
			PackageInfo& Info = PackageInfos[CurrentPackageIdx];

			Info.bHasEnums = true;
			Info.Enums.push_back(Obj.GetIndex());
		}
	}
}

void PackageManager::Init()
{
	if (bIsInitialized)
		return;

	bIsInitialized = true;

	PackageInfos.reserve(0x800);

	InitNameAndDependencies();
}
