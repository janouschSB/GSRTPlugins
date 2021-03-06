// Copyright 2019 (C) Ram�n Janousch

#include "APoolManager.h"
#include "Engine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PoolHolder.h"

AAPoolManager* AAPoolManager::Instance;

// Sets default values
AAPoolManager::AAPoolManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void AAPoolManager::BeginPlay()
{
	Instance = this;
	InitializePools();
	bIsReady = true;

	Super::BeginPlay();
}

UObject* AAPoolManager::GetFromPool(TSubclassOf<UObject> Class) {
	APoolHolder* PoolHolder;
	if (GetPoolHolder(Class, PoolHolder)) {
		if (PoolHolder->IsValidLowLevelFast()) {
			return PoolHolder->GetUnused();
		}
	}

	return nullptr;
}

UObject* AAPoolManager::GetSpecificFromPool(TSubclassOf<UObject> Class, FString ObjectName) {
	APoolHolder* PoolHolder;
	if (GetPoolHolder(Class, PoolHolder)) {
		if (PoolHolder->IsValidLowLevelFast()) {
			return PoolHolder->GetSpecific(ObjectName);
		}
	}

	return nullptr;
}

bool AAPoolManager::GetPoolHolder(TSubclassOf<UObject> Class, APoolHolder*& PoolHolder) {
	if (Class) {
		if (IsPoolManagerReady()) {
			FString Key = Class->GetName();
			if (Instance->ClassNamesToPools.Contains(Key)) {
				PoolHolder = *Instance->ClassNamesToPools.Find(Key);
				return true;
			}
		}

		UE_LOG(LogTemp, Error, TEXT("Pool Manager is not ready yet!"));
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Pass a valid class in which inherits from UObject!"));
	}

	return false;
}

TArray<UObject*> AAPoolManager::GetXFromPool(TSubclassOf<UObject> Class, int32 Quantity) {
	TArray<UObject*> Objects;
	for (int i = 0; i < Quantity; i++) {
		UObject* Object = GetFromPool(Class);
		if (Object->IsValidLowLevelFast()) break;
		
		Objects.Add(Object);
	}

	return Objects;
}

TArray<UObject*> AAPoolManager::GetAllFromPool(TSubclassOf<UObject> Class) {
	APoolHolder* PoolHolder;
	if (GetPoolHolder(Class, PoolHolder)) {
		if (PoolHolder->IsValidLowLevelFast()) {
			return PoolHolder->GetAllUnused();
		}
	}

	return TArray<UObject*>();
}

AActor* AAPoolManager::SpawnSpecificActorFromPool(TSubclassOf<AActor> Class, FString ObjectName, FTransform SpawnTransform, AActor* PoolOwner, APawn* PoolInstigator, EBranch& Branch) {
	if (Class) {
		AActor* UnusedActor = (AActor*)GetSpecificFromPool(Class, ObjectName);
		if (!IsValid(UnusedActor)) {
			Branch = EBranch::Failed;
			return NULL;
		}

		UnusedActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
		UnusedActor->SetOwner(PoolOwner);
		UnusedActor->Instigator = PoolInstigator;

		Branch = EBranch::Success;
		return UnusedActor;
	}

	UE_LOG(LogTemp, Error, TEXT("Pass a valid class in SpawnActorFromPool which inherits from Actor!"));
	Branch = EBranch::Failed;
	return NULL;
}

AActor* AAPoolManager::SpawnActorFromPool(TSubclassOf<AActor> Class, FTransform SpawnTransform, AActor* PoolOwner, APawn* PoolInstigator, EBranch& Branch) {
	if (Class) {
		AActor* UnusedActor = (AActor*) GetFromPool(Class);
		if (!IsValid(UnusedActor)) {
			Branch = EBranch::Failed;
			return NULL;
		}

		UnusedActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
		UnusedActor->SetOwner(PoolOwner);
		UnusedActor->Instigator = PoolInstigator;

		Branch = EBranch::Success;
		return UnusedActor;
	}

	UE_LOG(LogTemp, Error, TEXT("Pass a valid class in SpawnActorFromPool which inherits from Actor!"));
	Branch = EBranch::Failed;
	return NULL;
}

void AAPoolManager::InitializePools() {
	DestroyAllPools();

	for (auto& PoolSpecification : DesiredPools) {
		InitializeObjectPool(PoolSpecification);
	}
}

void AAPoolManager::ReturnToPool(UObject* Object, const EEndPlayReason::Type EndPlayReason) {
	APoolHolder* PoolHolder;
	if (!GetPoolHolder(Object->GetClass(), PoolHolder)) return;
	if (!IsValid(PoolHolder)) return;
	PoolHolder->ReturnObject(Object, EndPlayReason);
}

void AAPoolManager::EmptyObjectPool(TSubclassOf<UObject> Class) {
	if (Class) {
		if (!IsValid(Instance)) return;
		if (Instance->ClassNamesToPools.Num() == 0) return;
		Instance->bIsReady = false;

		APoolHolder* PoolHolder = *Instance->ClassNamesToPools.Find(*Class->GetName());
		if (!IsValid(PoolHolder)) return;

		PoolHolder->Destroy();
		Instance->ClassNamesToPools.Remove(*Class->GetName());
		Instance->bIsReady = true;
	}
}

void AAPoolManager::InitializeObjectPool(FPoolSpecification PoolSpecification) {
	APoolHolder* PoolHolder = Instance->GetWorld()->SpawnActor<APoolHolder>(APoolHolder::StaticClass(), Instance->GetTransform());
	PoolHolder->AttachToActor(Instance, FAttachmentTransformRules::KeepWorldTransform);

	PoolHolder->InitializePool(PoolSpecification);
	Instance->ClassNamesToPools.Add(PoolSpecification.Class->GetName(), PoolHolder);
}

FString AAPoolManager::GetObjectName(UObject* Object) {
	if (!Object->IsValidLowLevelFast()) return "None";

	FString FullName = Object->GetFullName();
	FString Path;
	FString Name;
	FullName.Split(":PersistentLevel.", &Path, &Name);

	return Name;
}

int32 AAPoolManager::GetNumberOfUsedObjects(TSubclassOf<UObject> Class) {
	APoolHolder* PoolHolder;
	if (!GetPoolHolder(Class, PoolHolder)) return -1;
	if (!IsValid(PoolHolder)) return -1;

	return PoolHolder->GetNumberOfUsedObjects();
}

int32 AAPoolManager::GetNumberOfAvailableObjects(TSubclassOf<UObject> Class) {
	APoolHolder* PoolHolder;
	if (!GetPoolHolder(Class, PoolHolder)) return -1;
	if (!IsValid(PoolHolder)) return -1;

	return PoolHolder->GetNumberOfAvailableObjects();
}

bool AAPoolManager::IsObjectActive(UObject* Object) {
	AActor* Actor = Cast<AActor>(Object);
	if (Actor->IsValidLowLevelFast()) {
		AActor* AttachedTo = Actor->GetAttachParentActor();
		if (AttachedTo->IsValidLowLevelFast()) {
			return AttachedTo->GetClass()->IsChildOf(APoolHolder::StaticClass());
		}
	}
	else {
		APoolHolder* PoolHolder;
		if (GetPoolHolder(Object->GetClass(), PoolHolder)) {
			if (PoolHolder->IsValidLowLevelFast()) {
				return !PoolHolder->IsObjectAvailable(Object);
			}
		}
	}

	return false;
}

bool AAPoolManager::ContainsClass(TSubclassOf<UObject> Class) {
	if (!Class) return false;
	FString Key = Class->GetName();
	return Instance->ClassNamesToPools.Contains(Key);
}

void AAPoolManager::DestroyAllPools() {
	TArray<APoolHolder*> Pools;
	ClassNamesToPools.GenerateValueArray(Pools);

	for (auto& PoolHolder : Pools) {
		PoolHolder->Destroy();
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (auto& Actor : AttachedActors) {
		Actor->Destroy();
	}
}

bool AAPoolManager::IsPoolManagerReady() {
	if (!IsValid(Instance)) return false;
	if (Instance->ClassNamesToPools.Num() == 0) return false;
	if (!Instance->bIsReady) return false;

	return true;
}