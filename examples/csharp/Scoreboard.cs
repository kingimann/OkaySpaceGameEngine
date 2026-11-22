using System.Collections.Generic;
using UnityEngine;
using Playground;   // PlayerMover lives in this namespace

// How hops turn into points. Swap in another class to change the rules.
public interface IScorer
{
    int Score(int hops);
}

public class Doubler : IScorer
{
    public int Score(int hops) => hops * 2;
}

// Every three seconds: reads the hops from the PlayerMover script on the same
// object, keeps a list of scores and logs the best one so far.
public class Scoreboard : MonoBehaviour
{
    private List<int> history = new List<int>();
    private IScorer scorer;
    private float timer;

    void Start()
    {
        scorer = new Doubler();
    }

    void Update()
    {
        timer += Time.deltaTime;
        if (timer < 3f) return;
        timer = 0f;

        PlayerMover mover = GetComponent<PlayerMover>();
        if (mover == null) return;
        int points = scorer.Score(mover.Hops);
        history.Add(points);

        int best = 0;
        foreach (int p in history)
            if (p > best) best = p;

        string rank;
        switch (best)
        {
            case 0: rank = "no hops yet"; break;
            case 2: case 4: rank = "warming up"; break;
            default: rank = "hopping well"; break;
        }
        Debug.Log($"Score {points}, best {best}: {rank}");
    }
}
