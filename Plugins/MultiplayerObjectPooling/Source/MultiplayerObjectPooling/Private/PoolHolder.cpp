// Copyright 2019 (C) Ram�n Janousch

#include "PoolHolder.h"
#include "Engine.h"
#include "PoolableInterface.h"


APoolHolder::APoolHolder() {
	PrimaryActorTick.bCanEverTick = false;
	// Add a root component to stick the pool on the pool manager
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void APoolHolder::Add(UObject* Object) {
	FString Name = Object->GetName();
	ObjectPool.Add(Name, Object);
	AvailableObjects.Add(Name);

	SetObjectActive(Object, false);

	// Add a timer and ignore the life span for actors with a life span
	if (DefaultObjectSettings.LifeSpan > 0) {
		Cast<AActor>(Object)->SetLifeSpan(0);
		ObjectsToTimers.Add(Object, FTimerHandle());
	}
}

UObject* APoolHolder::GetUnused() {
	if (AvailableObjects.Num() > 0) {
		UObject* UnusedObject = GetSpecificAndSetActive(AvailableObjects[0]);
		AvailableObjects.RemoveAt(0);

		return UnusedObject;
	}
	else {
		return nullptr;
	}
}

TArray<UObject*> APoolHolder::GetAllUnused() {
	TArray<UObject*> Objects;
	for (int i = 0; i < AvailableObjects.Num(); i++) {
		Objects.Add(GetSpecificAndSetActive(AvailableObjects[i]));
	}
	AvailableObjects.Empty();

	return Objects;
}

UObject* APoolHolder::GetSpecificAndSetActive(FString ObjectName) {
	UObject** ObjectFromPool = ObjectPool.Find(ObjectName);

	if (ObjectFromPool == nullptr) return nullptr;

	UObject* UnusedObject = *ObjectFromPool;

	SetObjectActive(UnusedObject);

	return UnusedObject;
}

UObject* APoolHolder::GetSpecific(FString ObjectName) {
	// TODO: Bad performance, find a better way
	AvailableObjects.Remove(ObjectName);

	return GetSpecificAndSetActive(ObjectName);
}

void APoolHolder::ReturnObject(UObject* Object, const EEndPlayReason::Type EndPlayReason) {
	FString ObjectName = Object->GetName();
	AvailableObjects.Add(ObjectName);

	SetObjectActive(Object, false, EndPlayReason);
}

void APoolHolder::SetObjectActive(UObject* Object, bool bIsActive, const EEndPlayReason::Type EndPlayReason) {
	if (!Object->IsValidLowLevelFast()) return;

	if (DefaultObjectSettings.bIsActor) {
		AActor* Actor = Cast<AActor>(Object);

		// Attach and detach the actor on the pool for better readability inside the editor
		if (bIsActive) {
			RestoreActorSettings(Actor);
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		else {
			Actor->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		}

		Actor->SetActorHiddenInGame(!bIsActive || DefaultObjectSettings.bHiddenInGame);
		Actor->SetActorEnableCollision(bIsActive);
		Actor->SetActorTickEnabled(bIsActive && DefaultObjectSettings.bStartWithTickEnabled);
	}

	if (DefaultObjectSettings.bImplementsPoolableInterface) {
		if (bIsActive) {
			IPoolableInterface::Execute_PoolableBeginPlay(Object);
		}
		else {
			IPoolableInterface::Execute_PoolableEndPlay(Object, EndPlayReason);
		}
	}
}

void APoolHolder::RestoreActorSettings(AActor* Actor) {
	// Restore default settings
	Actor->SetActorTickInterval(DefaultObjectSettings.TickInterval);
	Actor->bCanBeDamaged = DefaultObjectSettings.bCanBeDamaged;
	if (DefaultObjectSettings.LifeSpan > 0) {
		FTimerHandle* Timer = ObjectsToTimers.Find(Actor);
		FTimerDelegate TimerDel;
		TimerDel.BindUFunction(this, FName("ReturnObject"), Actor, EEndPlayReason::Destroyed);
		GetWorldTimerManager().SetTimer(*Timer, TimerDel, DefaultObjectSettings.LifeSpan, false);
	}

	// Restore default components settings
	if (DefaultComponentsSettings.Num() > 0) {
		TArray<UActorComponent*> ActorComponents;
		Actor->GetComponents<UActorComponent>(ActorComponents);

		for (int i = 0; i < ActorComponents.Num(); i++) {
			if (i >= DefaultComponentsSettings.Num()) return;

			// Restore actor component settings
			UActorComponent* ActorComponent = ActorComponents[i];
			FDefaultComponentSettings ComponentSettings = DefaultComponentsSettings[i];

			ActorComponent->SetComponentTickEnabled(ComponentSettings.bStartWithTickEnabled);
			ActorComponent->SetComponentTickInterval(ComponentSettings.TickInterval);
			ActorComponent->ComponentTags = ComponentSettings.Tags;
			ActorComponent->SetActive(ComponentSettings.bAutoActivate);

			// Restore scene component settings
			if (ComponentSettings.bIsSceneComponent) {
				USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent);
				if (SceneComponent->IsValidLowLevelFast()) {
					SceneComponent->SetRelativeTransform(ComponentSettings.RelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
					SceneComponent->SetVisibility(ComponentSettings.bIsVisible);
					SceneComponent->SetHiddenInGame(ComponentSettings.bIsHidden);

					// Restore static mesh component settings
					if (ComponentSettings.bIsStaticMeshComponent) {
						UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent);
						if (StaticMeshComponent->IsValidLowLevelFast()) {
							StaticMeshComponent->SetSimulatePhysics(ComponentSettings.bIsSimulatingPhysics);
						}
					}
				}
			}
		}
	}
}

void APoolHolder::InitializePool(FPoolSpecification PoolSpecification) {
	TSubclassOf<UObject> Class = PoolSpecification.Class;
	int32 NumberOfObjects = PoolSpecification.NumberOfObjects;

	if (Class) {
		// Save the default object settings
		DefaultObjectSettings.bImplementsPoolableInterface = Class->ImplementsInterface(UPoolableInterface::StaticClass());

		// Save the default actor settings
		if (Class->IsChildOf(AActor::StaticClass())) {
			AActor* DefaultActor = GetWorld()->SpawnActor(Class);
			DefaultObjectSettings.bIsActor = true;
			DefaultObjectSettings.bStartWithTickEnabled = DefaultActor->IsActorTickEnabled();
			DefaultObjectSettings.TickInterval = DefaultActor->GetActorTickInterval();
			DefaultObjectSettings.bHiddenInGame = DefaultActor->bHidden;
			DefaultObjectSettings.LifeSpan = DefaultActor->InitialLifeSpan;
			DefaultObjectSettings.bCanBeDamaged = DefaultActor->bCanBeDamaged;
			
			// Save the default component settings
			TArray<UActorComponent*> ActorComponents;
			DefaultActor->GetComponents<UActorComponent>(ActorComponents);
			for (int i = 0; i < ActorComponents.Num(); i++) {
				FDefaultComponentSettings DefaultComponentSettings;
				DefaultComponentSettings.bImplementsPoolableInterface = ActorComponents[i]->GetClass()->ImplementsInterface(UPoolableInterface::StaticClass());
				DefaultComponentSettings.bStartWithTickEnabled = ActorComponents[i]->IsComponentTickEnabled();
				DefaultComponentSettings.TickInterval = ActorComponents[i]->GetComponentTickInterval();
				DefaultComponentSettings.Tags = ActorComponents[i]->ComponentTags;
				DefaultComponentSettings.bAutoActivate = ActorComponents[i]->bAutoActivate;

				// Check for scene component settings
				USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponents[i]);
				if (SceneComponent->IsValidLowLevelFast()) {
					DefaultComponentSettings.bIsSceneComponent = true;
					DefaultComponentSettings.RelativeTransform = SceneComponent->GetRelativeTransform();
					DefaultComponentSettings.bIsVisible = SceneComponent->bVisible;
					DefaultComponentSettings.bIsHidden = SceneComponent->bHiddenInGame;

					// Check for static mesh component settings
					UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ActorComponents[i]);
					if (StaticMeshComponent->IsValidLowLevelFast()) {
						DefaultComponentSettings.bIsStaticMeshComponent = true;
						DefaultComponentSettings.bIsSimulatingPhysics = StaticMeshComponent->IsSimulatingPhysics();
					}
				}

				DefaultComponentsSettings.Add(DefaultComponentSettings);
			}
			DefaultActor->Destroy();
			
			for (int i = 0; i < NumberOfObjects; i++) {
				AActor* NewActor = GetWorld()->SpawnActor(Class);
				Add(NewActor);
			}
		}
		else {
			for (int i = 0; i < NumberOfObjects; i++) {
				Add(NewObject<UObject>((UObject*)GetTransientPackage(), Class));
			}
		}
	}
}

int32 APoolHolder::GetNumberOfUsedObjects() {
	return ObjectPool.Num() - AvailableObjects.Num();
}

int32 APoolHolder::GetNumberOfAvailableObjects() {
	return AvailableObjects.Num();
}

bool APoolHolder::IsObjectAvailable(UObject* Object) {
	return AvailableObjects.Contains(Object->GetName());
}

void APoolHolder::Destroyed() {
	if (DefaultObjectSettings.bIsActor) {
		TArray<UObject*> Pool;
		ObjectPool.GenerateValueArray(Pool);
		for (auto& Object : Pool) {
			AActor* Actor = Cast<AActor>(Object);
			Actor->Destroy();
		}
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (auto& Actor : AttachedActors) {
		Actor->Destroy();
	}

	AvailableObjects.Empty();

	// Clear all timers
	if (DefaultObjectSettings.LifeSpan > 0) {
		TArray<FTimerHandle> TimerHandles;
		ObjectsToTimers.GenerateValueArray(TimerHandles);
		for (auto& Timer : TimerHandles) {
			GetWorldTimerManager().ClearTimer(Timer);
		}
	}

	Super::Destroyed();
}