# VERSE Developer Guide

## Quick Start

## Overview
- Objective is a 1-to-1 match for all data on RPG 1.0
- Database will increase in size until a full capture of the Multiverse is achieved
- Bottlenecks will determine optimization work

## Storage
Upon first start, `${process.env.HOME}/.verse` is created:

```
$ tree ~/.verse
/home/fabric/.verse
└── core
    └── STATE

2 directories, 1 file
```

- `STATE` — contains a JSON-encoded state

## First Sync
VERSE will first load from RPG's "The Multiverse" which defines the "overworld" for the default game.
