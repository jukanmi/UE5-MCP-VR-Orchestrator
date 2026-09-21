import asyncio
from app.utils import db_manager

async def main():
    print("1. DB Init...")
    await db_manager.init_db()
    
    print("2. update_affinity_sync directly in memory...")
    await db_manager.start_background_sync()
    
    # Mock some data without starting the background loop
    # Call get_affinity first to cache it
    print("Loading into cache...")
    rel = await db_manager.get_affinity("Elara", "Player")
    print(f"Loaded: {rel}")
    
    print("Updating sync...")
    db_manager.update_affinity_sync("Elara", "Player", score_delta=15, interaction_summary="Spoke happily (+15)")
    
    print("3. Flush dirty cache...")
    await db_manager.stop_background_sync()
    
    print("Done. DB should have score=15 now.")

if __name__ == "__main__":
    asyncio.run(main())
