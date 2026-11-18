# Subgraph directions (#326)

A flowchart `subgraph` may carry a `direction` of its own. A group whose
nodes link **only to each other** is laid out along that direction and
then set into the diagram as one block; the diagram keeps *its* direction
for everything else. See Mermaid's
[direction in subgraphs](https://mermaid.js.org/syntax/flowchart.html#direction-in-subgraphs).
Every fence below but the last draws as a diagram.

## The reported sample

The diagram runs left to right, so its two unlinked groups share the first
rank and stack, as two unlinked nodes do. Inside each group the steps run
**top to bottom**.

```mermaid
flowchart LR
    subgraph current [Current]
        direction TB
        A1[Start] --> A2[Process] --> A3[Finish]
    end
    subgraph expected [Expected]
        direction TB
        B1[Start] --> B2[Review] --> B3[Finish]
    end
```

The same groups with no `direction` follow the diagram, left to right. The
two boxes stay clear of each other:

```mermaid
flowchart LR
    subgraph current [Current]
        A1[Start] --> A2[Process] --> A3[Finish]
    end
    subgraph expected [Expected]
        B1[Start] --> B2[Review] --> B3[Finish]
    end
```

## All four directions

| Statement | Runs | First node sits |
| --- | --- | --- |
| `direction TB` or `direction TD` | top to bottom | at the top |
| `direction BT` | bottom to top | at the bottom |
| `direction LR` | left to right | on the left |
| `direction RL` | right to left | on the right |

In a top-to-bottom diagram the four groups sit side by side:

```mermaid
flowchart TB
    subgraph down [TB]
        direction TB
        d1[One] --> d2[Two] --> d3[Three]
    end
    subgraph up [BT]
        direction BT
        u1[One] --> u2[Two] --> u3[Three]
    end
    subgraph right [LR]
        direction LR
        r1[One] --> r2[Two] --> r3[Three]
    end
    subgraph left [RL]
        direction RL
        l1[One] --> l2[Two] --> l3[Three]
    end
```

`TD` is `TB`, and a group may sit beside linked nodes of the diagram:

```mermaid
flowchart LR
    In([Request]) --> Check{Valid?}
    Check -->|yes| Done((Done))
    Check -- no --> Reject[Reject]
    subgraph steps [Steps of the check]
        direction TD
        s1[Read] --> s2[Parse] --> s3[Compare]
    end
```

## Nested groups

1. The outer group runs top to bottom.
2. Inside it, one group runs right to left and one bottom to top.
   - Each inner group is a block of the outer one.
   - The outer group is a block of the diagram.
3. `Source` and `Target` follow the diagram, left to right.

```mermaid
flowchart LR
    Source[Source] --> Target[Target]
    subgraph outer [Outer, top to bottom]
        direction TB
        o1[Collect] --> o2[Sort]
        subgraph inner1 [Inner, right to left]
            direction RL
            i1[First] --> i2[Second] --> i3[Third]
        end
        subgraph inner2 [Inner, bottom to top]
            direction BT
            j1[Low] --> j2[High]
        end
    end
```

> A group with no `direction` inside a group that has one follows that
> group:
>
> ```mermaid
> flowchart TB
>     subgraph lane [Lane, left to right]
>         direction LR
>         p1[Draft] --> p2[Review]
>         subgraph sign [Sign-off]
>             p3[Approve] --> p4[Publish]
>         end
>         p2 --> p3
>     end
>     Start[Start] --> End[End]
> ```

## A group linked to the outside

Mermaid ignores the `direction` of a group when one of its nodes is linked
to a node outside it, and the group takes its parent's direction. Here
`Kept` has no such link and stays top to bottom; `Linked` is entered from
`Outside` and runs left to right with the diagram:

```mermaid
flowchart LR
    subgraph kept [Kept]
        direction TB
        top1[top] --> bottom1[bottom]
    end
    subgraph linked [Linked]
        direction TB
        top2[top] --> bottom2[bottom]
    end
    Outside[Outside] --> top2
```

An inner group with links of its own only keeps its direction inside an
outer group that is linked to the outside:

```mermaid
flowchart TB
    subgraph service [Service]
        direction LR
        api[API] --> worker[Worker]
        subgraph retry [Retry loop]
            direction LR
            try1[Try] --> wait1[Wait] --> again[Try again]
        end
    end
    Client[Client] --> api
    worker --> Store[(Store)]
```

## Labels, loops and styles inside a group

Edge labels, a link back to an earlier step, a link from a node to itself,
node shapes and `classDef` colours inside groups with a direction:

```mermaid
flowchart LR
    classDef warn fill:#fff7ed,stroke:#f97316,color:#7c2d12,stroke-width:2px
    subgraph review [Review]
        direction TB
        w1([Write]) -->|send for review| w2{Approved?}
        w2 -->|yes| w3[Merge]
        w2 -- no, a second round --> w1
    end
    subgraph build [Build]
        direction TB
        b1[Compile] --> b2[Test]:::warn
        b2 -->|flaky| b2
        b2 ==> b3>Publish]
    end
    subgraph notes [Notes]
        direction RL
        n1[Changelog] -.-> n2[Release notes]
    end
```

## Groups without a direction

Nothing changes for groups that carry no `direction`, except that
neighbouring boxes no longer touch. Nested groups that end and start on
neighbouring ranks:

```mermaid
flowchart TB
    subgraph north [North]
        subgraph northInner [Inner]
            a[Plan] --> b[Build]
        end
    end
    subgraph south [South]
        subgraph southInner [Inner]
            c[Ship] --> d[Watch]
        end
    end
    b --> c
```

Three groups stacked in a left-to-right diagram, the middle one nested:

```mermaid
flowchart LR
    subgraph one [One]
        a1[Alpha] --> a2[Beta]
    end
    subgraph two [Two]
        subgraph twoInner [Inner]
            b1[Gamma] --> b2[Delta]
        end
    end
    subgraph three [Three]
        c1[Epsilon] --> c2[Zeta]
    end
```

## An unsupported direction

A value that is not `TB`, `TD`, `BT`, `LR` or `RL` is an error, with its
line, as an unsupported direction after `flowchart` is. Such a fence shows
its source in place of a diagram, as the first one below does; the second
is a plain code block to compare it with:

```mermaid
flowchart LR
    subgraph odd [Odd]
        direction XY
        x1 --> x2
    end
```

```text
flowchart LR
    subgraph odd [Odd]
        direction TB
        x1 --> x2
    end
```
